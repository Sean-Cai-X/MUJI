// src/adapters/MeCabAdapter.cpp
// Glue-layer adapter for MeCab — subprocess popen mode.
//
// 调用原生 mecab 二进制 (Debian 版, 位于 /H/usr/aisrc/mecab-bin/usr/bin/mecab)
// 解析 ChaSen 格式输出 → muji::SemToken
// 零 C API 链接依赖 — 完全符合"胶水层调用原生模块"架构

#include "MeCabAdapter.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>

namespace muji {

MeCabAdapter::MeCabAdapter() : AbstractAdapter("MeCab") {}

MeCabAdapter::~MeCabAdapter() {
    shutdown();
}

bool MeCabAdapter::init() {
    // 1. 定位 mecab 二进制
    std::vector<std::string> candidates = {
        config_.mecab_bin,
        getenv("MUJI_MECAB_BIN") ? getenv("MUJI_MECAB_BIN") : "",
        "/H/usr/aisrc/mecab-bin/usr/bin/mecab",
        "/usr/local/bin/mecab",
        "/usr/bin/mecab",
    };
    // 过滤空字符串
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
            [](const std::string& s){ return s.empty(); }),
        candidates.end());

    resolved_bin_.clear();
    for (const auto& cand : candidates) {
        struct stat st;
        if (stat(cand.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            resolved_bin_ = cand;
            break;
        }
    }
    if (resolved_bin_.empty()) {
        set_last_error("mecab binary not found (tried MUJI_MECAB_BIN, /usr/local/bin, /usr/bin)");
        return false;
    }

    // 2. 验证 dicdir 可访问
    if (!config_.dicdir.empty()) {
        struct stat st;
        if (stat(config_.dicdir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            set_last_error("dicdir not found: " + config_.dicdir);
            return false;
        }
    }

    // 3. 快速探测: echo "test" | mecab -d <dicdir> -r /dev/null
    std::string cmd = build_command() + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) {
        set_last_error("popen failed for mecab");
        return false;
    }
    fputs("test\n", pipe);
    int rc = pclose(pipe);
    if (rc != 0) {
        set_last_error("mecab init probe failed (rc=" + std::to_string(rc) + "), check dicdir/rcfile");
        return false;
    }

    set_status(AdapterStatus::Ready);
    return true;
}

bool MeCabAdapter::shutdown() {
    resolved_bin_.clear();
    set_status(AdapterStatus::Shutdown);
    return true;
}

bool MeCabAdapter::reload() {
    shutdown();
    return init();
}

std::string MeCabAdapter::build_command() const {
    std::ostringstream cmd;

    // 前置: Debian mecab 二进制依赖 libmecab.so.2, 必须指定 LD_LIBRARY_PATH
    cmd << "LD_LIBRARY_PATH=/H/usr/aisrc/mecab-bin/usr/lib/x86_64-linux-gnu";
    // 如果系统有额外 mecab lib, 也加上
    if (const char* extra = getenv("MUJI_MECAB_LIBPATH")) {
        cmd << ":" << extra;
    }
    cmd << " ";

    cmd << resolved_bin_;

    // dicdir (必须, 除非 mecabrc 指定了)
    if (!config_.dicdir.empty()) {
        cmd << " -d \"" << config_.dicdir << "\"";
    }
    // rcfile (空则 /dev/null, 避免 mecab 找 /etc/mecabrc)
    if (!config_.rcfile.empty()) {
        cmd << " -r \"" << config_.rcfile << "\"";
    } else {
        cmd << " -r /dev/null";
    }
    if (!config_.userdic.empty()) {
        cmd << " -u \"" << config_.userdic << "\"";
    }
    if (config_.partial)  cmd << " -p";
    if (config_.marginal) cmd << " -m";
    if (config_.nb1est > 1) cmd << " -N" << config_.nb1est;

    // ChaSen 格式输出 (默认就是, 显式指定更安全)
    cmd << " -O chasen";

    return cmd.str();
}

bool MeCabAdapter::segment(const std::string& text,
                           std::vector<SemToken>& out) const {
    if (resolved_bin_.empty()) {
        const_cast<MeCabAdapter*>(this)->init();
        if (resolved_bin_.empty()) return false;
    }
    out.clear();
    if (text.empty()) return true;

    // popen 管道模式
    std::string cmd = build_command();
    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) return false;

    fwrite(text.data(), 1, text.size(), pipe);
    fputc('\n', pipe);
    fflush(pipe);
    pclose(pipe);  // mecab 从 stdin 读, 写完关闭触发处理

    // 用 popen("r") 重新跑一次读输出 — popen 不能同时读写
    // 用 <() 进程替换更简洁
    std::string rcmd = cmd + " <<'MUJI_EOF'\n" + text + "\nMUJI_EOF\n";
    // 但 <() 是 bash 特性, 用 sh -c 包裹
    std::string full = "sh -c " + std::string("'") + cmd + " <<'MUJI_EOF'\n" + text + "\nMUJI_EOF\n'";

    FILE* rpipe = popen(full.c_str(), "r");
    if (!rpipe) return false;

    std::array<char, 4096> buf;
    std::string chasen_out;
    while (fgets(buf.data(), buf.size(), rpipe)) {
        chasen_out += buf.data();
    }
    pclose(rpipe);

    out = parse_chasen(chasen_out, text);
    return !out.empty();
}

std::vector<SemToken> MeCabAdapter::parse_chasen(
    const std::string& chasen_output,
    const std::string& original_text) {

    std::vector<SemToken> tokens;
    // 构建原始 text → byte offset 映射 (start/end 用)
    // MeCab 不输出 offset, 所以我们自己基于 surface 搜索
    size_t search_pos = 0;

    std::istringstream iss(chasen_output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line == "EOS") continue;

        // ChaSen 格式:
        // surface\tpos1,pos2,pos3,pos4,conj1,conj2,lemma,reading,pron
        size_t tab1 = line.find('\t');
        if (tab1 == std::string::npos) continue;

        std::string surface = line.substr(0, tab1);
        std::string feats   = line.substr(tab1 + 1);

        // 拆分 feats (逗号分隔)
        std::vector<std::string> fields;
        {
            std::string tmp = feats;
            while (!tmp.empty()) {
                size_t comma = tmp.find(',');
                if (comma == std::string::npos) {
                    fields.push_back(tmp);
                    break;
                }
                fields.push_back(tmp.substr(0, comma));
                tmp = tmp.substr(comma + 1);
            }
        }

        SemToken t;
        t.word = surface;
        t.source = SourceTag::MeCabMorpheme;
        t.is_oov = false;
        t.hit_user_dict = false;

        // pos = pos1-pos2 合并 (非空部分)
        if (fields.size() > 0 && !fields[0].empty()) {
            t.pos = fields[0];
            if (fields.size() > 1 && !fields[1].empty() && fields[1] != "*") {
                t.pos += "-" + fields[1];
            }
        }
        // lemma = fields[6], reading = fields[7] (ChaSen 固定位置)
        if (fields.size() > 6 && !fields[6].empty() && fields[6] != "*") {
            t.lemma = fields[6];
        } else {
            t.lemma = surface;  // fallback
        }
        if (fields.size() > 7 && !fields[7].empty() && fields[7] != "*") {
            t.reading = fields[7];
        }

        // byte offset (在 original_text 里找 surface)
        size_t found = original_text.find(surface, search_pos);
        if (found != std::string::npos) {
            t.start = static_cast<int32_t>(found);
            t.end   = static_cast<int32_t>(found + surface.size());
            search_pos = found + surface.size();
        } else {
            // fallback: 用上一个 token 的 end
            if (!tokens.empty()) {
                t.start = tokens.back().end;
                t.end   = t.start + static_cast<int32_t>(surface.size());
            } else {
                t.start = 0;
                t.end   = static_cast<int32_t>(surface.size());
            }
        }

        tokens.push_back(std::move(t));
    }
    return tokens;
}

} // namespace muji
