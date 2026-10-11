// src/adapters/UDPipeAdapter.h
// Glue-layer adapter for UDPipe (layer 0c — German + multi-language UDP pipeline).
//
// 原生入口: src/native/udpipe_cli.cpp (独立 CLI, 后续实现) + 编译好的 udpipe 二进制
//
// 胶水层三种模式 (mode 字段选择):
//   "cli"   — subprocess 调用本地 udpipe 二进制 + 本地模型文件 (离线)
//   "rest"  — HTTP POST 到 UDPipe REST 服务 (LINDAT 在线服务, 无需本地模型)
//   "auto"  — init 时先测 REST, 通就用 REST; 否则 fallback CLI
//
// 统一输出: muji::SemToken (与 Jieba/MeCab 完全一致)
//
// UDPipe 输出 CoNLL-U 格式 (每行一个 word, TAB 分隔 10 列):
//   ID    FORM   LEMMA   UPOS   XPOS   FEATS   HEAD   DEPREL   DEPS   MISC
//   1     Die    die     DET    ART    ...     2      det      ...    ...

#pragma once

#include "glue/AbstractAdapter.h"
#include "data/SemanticTypes.h"

#include <string>
#include <vector>
#include <memory>

namespace muji {

struct UDPipeConfig {
    std::string mode = "auto";        // "cli" | "rest" | "auto"
    std::string model_path;           // CLI 模式: 本地模型路径
    std::string model_name;           // REST 模式: 模型名 (默认 "german-gsd")
    std::string udpipe_bin;           // CLI 模式: udpipe 二进制路径
    std::string rest_url = "http://lindat.mff.cuni.cz/services/udpipe/api/process";
    bool        tokenize  = true;
    bool        tag       = true;      // 启用 UPOS + XPOS
    bool        lemmatize = true;      // 启用 lemma
    bool        parse     = false;     // 依存句法 (可选)
    std::string upos_mode = "upos";   // "upos" (Universal) / "xpos" (STTS for German)
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
    std::string resolved_mode_ = "cli";   // init() 后确定的实际模式

    // CoNLL-U 解析 (CLI 和 REST 共享)
    static std::vector<SemToken> parse_conllu(const std::string& conllu_output,
                                               const std::string& original_text,
                                               const UDPipeConfig& cfg);

    // 两种后端实现
    std::string call_cli(const std::string& text) const;
    std::string call_rest(const std::string& text) const;

    // decompound_cli JSON → CompoundSplit (极简 parser)
    std::vector<CompoundSplit> parse_decompound_json(
        const std::string& json, const std::string& original_word) const;

    // 探测 REST 连通性
    bool probe_rest() const;
};

} // namespace muji
