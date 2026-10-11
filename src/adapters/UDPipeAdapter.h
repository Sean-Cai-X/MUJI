// src/adapters/UDPipeAdapter.h
// Glue-layer adapter for UDPipe (layer 0c — German + multi-language UDP pipeline).
//
// 原生入口: src/native/udpipe_cli.cpp (独立 CLI) + 系统 udpipe 二进制
//
// 胶水层模式:
//   优先调用 UDPipe C++ API (如果已安装)；
//   否则 fallback 到 subprocess 调用 `udpipe --outfmt conllu model` 并解析 CoNLL-U 输出。
//
// 统一输出: muji::SemToken (与 Jieba/MeCab 完全一致)
//
// UDPipe 输出 CoNLL-U 格式 (每行一个 word, TAB 分隔 10 列):
//   ID    FORM   LEMMA   UPOS   XPOS   FEATS   HEAD   DEPREL   DEPS   MISC
//   1     Die    die     DET    ART    ...     2      det      ...    ...
//
// UDPipe 模型文件 (示例):
//   german-gsd.udpipe   — Universal Dependencies German GSD
//   german-hdt.udpipe   — German HDT (科研场景优先)

#pragma once

#include "glue/AbstractAdapter.h"
#include "data/SemanticTypes.h"

#include <string>
#include <vector>
#include <memory>

namespace muji {

struct UDPipeConfig {
    std::string model_path;          // UDPipe 模型 e.g. "de/german-gsd.udpipe"
    std::string udpipe_bin;          // udpipe CLI 路径 (默认从 PATH 找)
    bool        tokenize = true;
    bool        tag      = true;     // 启用 UPOS + XPOS
    bool        lemmatize = true;    // 启用 lemma
    bool        parse    = false;     // 依存句法 (可选)
    std::string upos_mode = "upos";  // "upos" (Universal) / "xpos" (STTS for German)
    bool        prefer_cli = true;   // true=subprocess CLI, false=direct C++ API (如可用)
};

class UDPipeAdapter : public AbstractAdapter {
public:
    UDPipeAdapter();
    ~UDPipeAdapter() override;

    bool init() override;
    bool shutdown() override;

    // ── Query API ───────────────────────────────────────────────────
    bool segment(const std::string& text,
                 std::vector<SemToken>& out) const;

    std::vector<SemToken> segment(const std::string& text) const {
        std::vector<SemToken> r;
        segment(text, r);
        return r;
    }

    // ── German compound word split (启发式, 可对接 decompound 原生 CLI) ──
    std::vector<CompoundSplit> split_compound(
        const std::string& compound_word,
        int max_candidates = 3) const;

    // ── Config ─────────────────────────────────────────────────────
    void set_config(const UDPipeConfig& c) { config_ = c; }
    const UDPipeConfig& config() const     { return config_; }

    bool reload();

private:
    UDPipeConfig config_;
    bool initialized_ = false;

    // CoNLL-U 解析辅助
    static std::vector<SemToken> parse_conllu(const std::string& conllu_output,
                                               const std::string& original_text,
                                               const UDPipeConfig& cfg);
};

} // namespace muji
