// src/adapters/UDPipeAdapter.cpp
// Glue-layer adapter for UDPipe — CLI subprocess + REST API 双模式
//
// mode="cli"   → subprocess 调用本地 udpipe 二进制 + 本地模型文件 (离线)
// mode="rest"  → HTTP POST UDPipe REST 服务 (LINDAT, 无需本地模型)
// mode="auto"  → init 时先探测 REST 连通性, 通就 REST; 否则 fallback CLI
//
// 统一输出: muji::SemToken (与 Jieba/MeCab 完全一致)
// CoNLL-U 格式 (CLI 和 REST 共享):
//   ID  FORM  LEMMA  UPOS  XPOS  FEATS  HEAD  DEPREL  DEPS  MISC

#include "UDPipeAdapter.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <array>
#include <memory>

namespace muji {

// ── popen 通用 helper ──
static std::string run_cmd(const std::string& cmd) {
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return {};
    std::string out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    pclose(fp);
    return out;
}

UDPipeAdapter::UDPipeAdapter() : AbstractAdapter("UDPipe") {}
UDPipeAdapter::~UDPipeAdapter() { if (initialized_) shutdown(); }

bool UDPipeAdapter::probe_rest() const {
    // REST 服务不支持 HEAD (501), 用 GET 看有没有返回 JSON/错误消息 (说明在线)
    std::string url = config_.rest_url;
    if (url.empty()) url = "http://lindat.mff.cuni.cz/services/udpipe/api/process";
    std::string cmd = "curl -s --connect-timeout 3 --max-time 5 '" + url + "' 2>&1 | head -1";
    std::string out = run_cmd(cmd);
    // GET 不带参数会返回 "parameter 'data' is required" (服务在线的证明)
    return !out.empty() && (out.find("parameter") != std::string::npos ||
                            out.find("required") != std::string::npos ||
                            out.find("json") != std::string::npos ||
                            out.find("UDPipe") != std::string::npos);
}

bool UDPipeAdapter::init() {
    if (initialized_) return true;

    // ── 确定实际模式 ──
    resolved_mode_ = config_.mode;
    if (resolved_mode_ == "auto") {
        // 优先 REST (无需模型文件), 不通再 CLI
        if (probe_rest()) {
            resolved_mode_ = "rest";
        } else {
            resolved_mode_ = "cli";
        }
    }

    // ── REST 模式 ──
    if (resolved_mode_ == "rest") {
        // REST 不需要本地模型文件, 只需填 model_name
        if (config_.model_name.empty()) config_.model_name = "german-gsd";
        if (!probe_rest()) {
            set_last_error("UDPipe REST service unreachable: " + config_.rest_url);
            return false;
        }
        initialized_ = true;
        set_status(AdapterStatus::Ready);
        return true;
    }

    // ── CLI 模式 ──
    if (resolved_mode_ == "cli") {
        // 1. 模型文件
        if (config_.model_path.empty()) {
            set_last_error("UDPipeConfig.model_path empty (CLI mode needs local model)");
            return false;
        }
        if (access(config_.model_path.c_str(), R_OK) != 0) {
            set_last_error("UDPipe model not found: " + config_.model_path);
            return false;
        }

        // 2. 找 udpipe CLI
        std::string bin = config_.udpipe_bin;
        if (bin.empty()) {
            // 先查 /H/usr/aisrc/udpipe/udpipe (我们编译的)
            for (auto* p : {
                "/H/usr/aisrc/udpipe/udpipe",
                "/H/aisrc/MUJI/toolchain-rootfs/usr/bin/udpipe",
                "/usr/bin/udpipe",
                "/usr/local/bin/udpipe"
            }) {
                if (access(p, X_OK) == 0) { bin = p; break; }
            }
            if (bin.empty()) {
                const char* path_env = std::getenv("PATH");
                if (path_env) {
                    std::istringstream ss(path_env);
                    std::string dir;
                    while (std::getline(ss, dir, ':')) {
                        std::string cand = dir + "/udpipe";
                        if (access(cand.c_str(), X_OK) == 0) { bin = cand; break; }
                    }
                }
            }
        }
        if (bin.empty()) {
            set_last_error("udpipe CLI not found (need /H/usr/aisrc/udpipe/udpipe or in PATH)");
            return false;
        }
        config_.udpipe_bin = bin;
    }

    initialized_ = true;
    set_status(AdapterStatus::Ready);
    return true;
}

bool UDPipeAdapter::shutdown() {
    initialized_ = false;
    set_status(AdapterStatus::Shutdown);
    return true;
}

bool UDPipeAdapter::reload() {
    shutdown();
    return init();
}

// ── CLI 后端: subprocess 调用 udpipe ──
std::string UDPipeAdapter::call_cli(const std::string& text) const {
    // 写临时输入
    char in_fname[]  = "/tmp/muji_udp_in_XXXXXX";
    char out_fname[] = "/tmp/muji_udp_out_XXXXXX";
    int infd  = mkstemp(in_fname);
    int outfd = mkstemp(out_fname);
    if (infd < 0 || outfd < 0) return {};

    ssize_t w = write(infd, text.data(), text.size());
    close(infd);
    if (w != (ssize_t)text.size()) { close(outfd); unlink(in_fname); unlink(out_fname); return {}; }

    std::string cmd = config_.udpipe_bin;
    if (config_.tokenize)  cmd += " --tokenize";
    if (config_.tag)       cmd += " --tag";
    if (config_.lemmatize) cmd += " --lemmatize";
    if (config_.parse)     cmd += " --parse";
    cmd += " --output=conllu " + config_.model_path;
    cmd += " < " + std::string(in_fname) + " > " + out_fname + " 2>/dev/null";

    int rc = std::system(cmd.c_str());
    (void)rc;

    close(outfd);
    std::string result;
    FILE* ofp = fopen(out_fname, "r");
    if (ofp) { char buf[4096]; while (fgets(buf, sizeof(buf), ofp)) result += buf; fclose(ofp); }
    unlink(in_fname);
    unlink(out_fname);
    return result;
}

// ── REST 后端: curl POST 到 LINDAT ──
// 返回 CoNLL-U 格式 (从 JSON .result 字段提取)
std::string UDPipeAdapter::call_rest(const std::string& text) const {
    // curl POST URL-encoded form
    std::string url = config_.rest_url;
    if (url.empty()) url = "http://lindat.mff.cuni.cz/services/udpipe/api/process";

    // URL-encode text for POST body
    auto urlencode = [](const std::string& s) {
        std::string r;
        r.reserve(s.size());
        for (unsigned char c : s) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') r += c;
            else if (c == ' ') r += '+';
            else { char hex[4]; snprintf(hex, sizeof(hex), "%%%02X", c); r += hex; }
        }
        return r;
    };

    std::string body = "model=" + urlencode(config_.model_name)
                    + "&data=" + urlencode(text)
                    + "&tokenizer=" + std::string(config_.tokenize ? "1" : "0")
                    + "&tagger=" + std::string(config_.tag ? "1" : "0")
                    + "&parser=" + std::string(config_.parse ? "1" : "0");

    // 用 curl POST → 拿到 JSON → 提取 .result 字段
    std::string curl_cmd = "curl -sS --connect-timeout 15 --max-time 60 "
                          "-X POST '" + url + "' "
                          "-H 'Content-Type: application/x-www-form-urlencoded' "
                          "-d '" + body + "' 2>/dev/null";

    std::string json = run_cmd(curl_cmd);
    if (json.empty()) return {};

    // 简易 JSON 解析: 找 "result":"..." 或 "result": "..."
    // REST 服务返回 {"result": "# ... CoNLL-U ..."}
    size_t key = json.find("\"result\"");
    if (key == std::string::npos) return {};
    size_t colon = json.find(':', key);
    if (colon == std::string::npos) return {};
    size_t start = colon + 1;
    while (start < json.size() && (json[start] == ' ' || json[start] == '"')) start++;
    size_t end = json.rfind('"');
    if (end <= start) return {};

    std::string raw = json.substr(start, end - start);

    // unescape JSON: backslash-n → newline, backslash-t → tab, backslash-quote → quote
    std::string conllu;
    conllu.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            char n = raw[i + 1];
            if (n == 'n') { conllu += '\n'; ++i; continue; }
            if (n == 't') { conllu += '\t'; ++i; continue; }
            if (n == '"') { conllu += '"'; ++i; continue; }
            if (n == '\\') { conllu += '\\'; ++i; continue; }
            conllu += raw[i];
        } else {
            conllu += raw[i];
        }
    }
    return conllu;
}

// ── CoNLL-U 解析 ──
std::vector<SemToken> UDPipeAdapter::parse_conllu(
    const std::string& conllu,
    const std::string& original_text,
    const UDPipeConfig& cfg) {

    std::vector<SemToken> out;
    std::istringstream iss(conllu);
    std::string line;
    int32_t cursor = 0;

    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> cols;
        std::string col;
        std::istringstream lss(line);
        while (std::getline(lss, col, '\t')) cols.push_back(col);
        if (cols.size() < 4) continue;

        auto& id = cols[0];
        if (id.find('-') != std::string::npos) continue;  // skip multi-word tokens

        SemToken t;
        t.word  = cols[1];
        t.lemma = cols[2];
        if (cfg.upos_mode == "xpos" && cols.size() > 4 && !cols[4].empty())
            t.pos = cols[4];
        else if (!cols[3].empty())
            t.pos = cols[3];
        else
            t.pos = "?";

        size_t found = original_text.find(t.word, cursor);
        if (found == std::string::npos) found = cursor;
        t.start = static_cast<int32_t>(found);
        t.end   = static_cast<int32_t>(found + t.word.size());
        t.source = SourceTag::UDPipeToken;

        cursor = t.end;
        out.push_back(std::move(t));
    }
    return out;
}

bool UDPipeAdapter::segment(const std::string& text,
                            std::vector<SemToken>& out) const {
    if (!initialized_) return false;
    out.clear();
    if (text.empty()) return true;

    std::string conllu;
    if (resolved_mode_ == "rest") {
        conllu = call_rest(text);
    } else {
        conllu = call_cli(text);
    }

    if (!conllu.empty()) {
        out = parse_conllu(conllu, text, config_);
    }
    return !out.empty();
}

// ── 德语复合词拆分 (启发式 fallback) ──
std::vector<CompoundSplit> UDPipeAdapter::split_compound(
    const std::string& compound_word,
    int max_candidates) const {

    std::vector<CompoundSplit> result;
    if (compound_word.empty()) return result;

    static const char* suffixes[] = {
        "ung", "heit", "keit", "tion", "ität", "isch", "lich", "los",
        "station", "haus", "werk", "karte", "speicher", "platz", "kraft",
        "handy", "auflade", "fern", "seh", "fernseher"
    };
    const int n_suffix = sizeof(suffixes) / sizeof(suffixes[0]);

    // Candidate 0: 整体不拆分
    {
        CompoundSplit cs;
        cs.original = compound_word;
        cs.confidence = 0.5f;
        cs.parts.push_back({compound_word, compound_word, "NOUN", 0, (int32_t)compound_word.size()});
        result.push_back(std::move(cs));
    }

    for (int si = 0; si < n_suffix && (int)result.size() < max_candidates; ++si) {
        const char* suf = suffixes[si];
        size_t slen = strlen(suf);
        size_t pos = compound_word.find(suf);
        if (pos != std::string::npos && pos + slen < compound_word.size()) {
            CompoundSplit cs;
            cs.original = compound_word;
            cs.confidence = 0.35f;
            std::string first = compound_word.substr(0, pos + slen);
            std::string rest  = compound_word.substr(pos + slen);
            cs.parts.push_back({first, first, "NOUN", 0, (int32_t)first.size()});
            cs.parts.push_back({rest, rest, "NOUN", (int32_t)first.size(), (int32_t)compound_word.size()});
            result.push_back(std::move(cs));
        }
    }
    return result;
}

} // namespace muji
