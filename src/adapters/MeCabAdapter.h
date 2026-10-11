// src/adapters/MeCabAdapter.h
// Glue-layer adapter for MeCab (layer 0b — Japanese morphological analysis).
//
// Native entry point: /usr/local/bin/mecab (Debian 原生二进制, 独立运行/升级).
// MeCabAdapter uses subprocess popen to call the native mecab binary,
// parsing ChaSen-format output → muji::SemToken. Zero C API link dependency.

#pragma once

#include "glue/AbstractAdapter.h"
#include "data/SemanticTypes.h"

#include <string>
#include <vector>

namespace muji {

struct MeCabConfig {
    std::string dicdir;        // 词典目录 e.g. ".../ipadic-utf8"
    std::string rcfile;        // mecabrc 路径 (空=/dev/null)
    std::string userdic;       // 用户词典 (可选)
    std::string mecab_bin;     // mecab 二进制路径 (空=自动搜)
    bool        partial = false;
    bool        marginal = false;
    int         nb1est = 1;
};

class MeCabAdapter : public AbstractAdapter {
public:
    MeCabAdapter();
    ~MeCabAdapter() override;

    bool init() override;
    bool shutdown() override;

    bool segment(const std::string& text,
                 std::vector<SemToken>& out) const;

    std::vector<SemToken> segment(const std::string& text) const {
        std::vector<SemToken> r;
        segment(text, r);
        return r;
    }

    void set_config(const MeCabConfig& c) { config_ = c; }
    const MeCabConfig& config() const     { return config_; }

    bool reload();

    // 解析 ChaSen 格式 mecab 输出 → SemToken 列表 (static, 可独立测试)
    static std::vector<SemToken> parse_chasen(
        const std::string& chasen_output,
        const std::string& original_text);

private:
    MeCabConfig config_;
    std::string resolved_bin_;   // init() 后确定的 mecab 二进制完整路径

    // 构造完整 mecab 命令行 (含所有 flag + dicdir + rcfile)
    std::string build_command() const;
};

} // namespace muji
