// src/adapters/MeCabAdapter.h
// Glue-layer adapter for MeCab (layer 0b — Japanese morphological analysis).
//
// Native entry point preserved: see src/native/mecab_cli.cpp
// (standalone command-line tool, compiles independently, bypasses ImGui
// and the glue layer entirely — the module stays 100% upgradeable).
//
// This adapter only:
//   - forwards calls to MeCab C API (mecab_new / mecab_parse_lattice)
//   - converts MeCab morpheme output → muji::SemToken
//   - fills SemToken.lemma and SemToken.reading (MeCab 扩展字段)
//   - does NOT modify MeCab logic or hold MeCab handle after shutdown

#pragma once

#include "glue/AbstractAdapter.h"
#include "data/SemanticTypes.h"

#include <string>
#include <vector>
#include <memory>

// fwd — only #include <mecab.h> in .cpp
struct mecab_t;

namespace muji {

struct MeCabConfig {
    std::string dicdir;        // 词典目录 e.g. "/var/lib/mecab/dic/ipadic-utf8"
    std::string rcfile;        // mecabrc 路径 (空=用内置默认)
    std::string userdic;       // 用户词典 (可选)
    bool        partial = false;  // 部分解析模式 (partial parsing)
    bool        marginal = false;  // 边缘概率模式
    int         nb1est = 1;      // N-best 候选数 (默认 1)
};

class MeCabAdapter : public AbstractAdapter {
public:
    MeCabAdapter();
    ~MeCabAdapter() override;

    bool init() override;
    bool shutdown() override;

    // ── Query API ───────────────────────────────────────────────────
    // Morphologically analyze text → tokens.
    // Fills SemToken.{word, lemma, pos, reading, start, end, source}.
    // SourceTag = MeCabMorpheme (区别于 JiebaToken).
    bool segment(const std::string& text,
                 std::vector<SemToken>& out) const;

    // Convenience overload
    std::vector<SemToken> segment(const std::string& text) const {
        std::vector<SemToken> r;
        segment(text, r);
        return r;
    }

    // ── Config ─────────────────────────────────────────────────────
    void set_config(const MeCabConfig& c) { config_ = c; }
    const MeCabConfig& config() const     { return config_; }

    bool reload();

private:
    MeCabConfig config_;
    mecab_t*    mecab_ = nullptr;   // C API handle (not owning via unique_ptr)
};

} // namespace muji
