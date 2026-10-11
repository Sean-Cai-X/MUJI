// src/adapters/UDPipeAdapter.cpp
// Glue-layer adapter for UDPipe (multi-language UDP pipeline).
//
// 胶水层策略: subprocess 调用 udpipe CLI, 解析 CoNLL-U 格式输出。
// 这样 UDPipeAdapter 不需要知道 UDPipe 的任何 C/C++ API。
//
// 独立原生入口: src/native/udpipe_cli.cpp (直接调用 udpipe CLI)
//
// 统一输出: muji::SemToken (与 Jieba/MeCab 完全一致)

#include "UDPipeAdapter.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>

namespace muji {

UDPipeAdapter::UDPipeAdapter() : AbstractAdapter("UDPipe") {}
UDPipeAdapter::~UDPipeAdapter() { if (initialized_) shutdown(); }

bool UDPipeAdapter::init() {
    if (initialized_) return true;

    // 1. 检查 udpipe 模型文件
    if (config_.model_path.empty()) {
        set_last_error("UDPipeConfig.model_path is empty");
        return false;
    }
    if (access(config_.model_path.c_str(), R_OK) != 0) {
        set_last_error("UDPipe model not found or not readable: " + config_.model_path);
        return false;
    }

    // 2. 找 udpipe CLI
    std::string udpipe_bin = config_.udpipe_bin;
    if (udpipe_bin.empty()) {
        const char* path_env = std::getenv("PATH");
        if (path_env) {
            std::istringstream ss(path_env);
            std::string dir;
            while (std::getline(ss, dir, ':')) {
                std::string candidate = dir + "/udpipe";
                if (access(candidate.c_str(), X_OK) == 0) {
                    udpipe_bin = candidate;
                    break;
                }
            }
        }
    }
    if (udpipe_bin.empty()) {
        for (auto* p : {
            "/H/aisrc/MUJI/toolchain-rootfs/usr/bin/udpipe",
            "/usr/bin/udpipe",
            "/usr/local/bin/udpipe"
        }) {
            if (access(p, X_OK) == 0) { udpipe_bin = p; break; }
        }
    }
    if (udpipe_bin.empty()) {
        set_last_error("udpipe CLI not found in PATH or standard locations");
        return false;
    }

    config_.udpipe_bin = udpipe_bin;
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

        // CoNLL-U 10 columns, TAB-separated
        std::vector<std::string> cols;
        std::string col;
        std::istringstream lss(line);
        while (std::getline(lss, col, '\t')) cols.push_back(col);
        if (cols.size() < 4) continue;

        auto& id = cols[0];
        if (id.find('-') != std::string::npos) continue;  // skip multi-word tokens

        SemToken t;
        t.word = cols[1];
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

    // 构造 udpipe 命令行
    std::string cmd = config_.udpipe_bin;
    if (config_.tokenize)  cmd += " --tokenize";
    if (config_.tag)       cmd += " --tag";
    if (config_.parse)     cmd += " --parse";
    cmd += " --output=conllu " + config_.model_path;

    // 用临时文件传递 stdin/stdout
    char in_fname[]  = "/tmp/muji_udp_in_XXXXXX";
    char out_fname[] = "/tmp/muji_udp_out_XXXXXX";
    int infd  = mkstemp(in_fname);
    int outfd = mkstemp(out_fname);
    if (infd < 0 || outfd < 0) {
        if (infd >= 0)  close(infd);
        if (outfd >= 0) close(outfd);
        return false;
    }

    // 写输入
    ssize_t written = write(infd, text.data(), text.size());
    close(infd);
    if (written != (ssize_t)text.size()) {
        close(outfd); unlink(in_fname); unlink(out_fname);
        return false;
    }

    // 执行: cmd < in_fname > out_fname
    std::string full_cmd = cmd + " < " + in_fname + " > " + out_fname + " 2>/dev/null";
    int rc = std::system(full_cmd.c_str());
    (void)rc;  // 即使 udpipe 有 warning, 输出文件可能仍有内容

    // 读输出
    close(outfd);
    std::string conllu_output;
    FILE* ofp = fopen(out_fname, "r");
    if (ofp) {
        char buf[4096];
        while (fgets(buf, sizeof(buf), ofp)) conllu_output += buf;
        fclose(ofp);
    }

    unlink(in_fname);
    unlink(out_fname);

    // 解析 CoNLL-U
    if (!conllu_output.empty()) {
        out = parse_conllu(conllu_output, text, config_);
    }
    return !out.empty();
}

// ── 德语复合词拆分 (启发式 fallback, 可对接 decompound 原生 CLI) ──
std::vector<CompoundSplit> UDPipeAdapter::split_compound(
    const std::string& compound_word,
    int max_candidates) const {

    std::vector<CompoundSplit> result;
    if (compound_word.empty()) return result;

    // 德语常见复合词后缀
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

    // 每个后缀尝试作为拆分点
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
