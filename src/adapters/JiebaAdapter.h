// src/adapters/JiebaAdapter.h
// Glue-layer adapter for cppjieba (layer 0 — Chinese segmentation).
//
// Native entry point preserved: see src/native/jieba_cli.cpp
// (standalone command-line tool, compiles independently, bypasses ImGui
// and the glue layer entirely — the module stays 100% upgradeable).
//
// This adapter only:
//   - forwards calls to cppjieba header API
//   - converts cppjieba's Cut() output → muji::SemToken
//   - does NOT modify cppjieba logic or hold Jieba instance after shutdown

#pragma once

#include "glue/AbstractAdapter.h"
#include "data/SemanticTypes.h"

#include <string>
#include <vector>
#include <memory>

namespace cppjieba { class Jieba; } // fwd — only include in .cpp

namespace muji {

struct JiebaConfig {
    std::string dict_path    = "3rdparty/cppjieba/dict/jieba.dict.utf8";
    std::string hmm_path     = "3rdparty/cppjieba/dict/hmm_model.utf8";
    std::string user_dict_path;   // optional user/custom dict
    std::string idf_path     = "3rdparty/cppjieba/dict/idf.utf8";
    std::string stop_words_path = "3rdparty/cppjieba/dict/stop_words.utf8";
    enum Mode { Exact = 0, Full = 1, Search = 2 };
    Mode mode = Exact;
};

class JiebaAdapter : public AbstractAdapter {
public:
    JiebaAdapter();
    ~JiebaAdapter() override;

    bool init() override;
    bool shutdown() override;

    // ── Query API ───────────────────────────────────────────────────
    // Segment text into tokens. Fills source_pos on each SemToken.
    // Returns true on success (empty result is still success).
    bool segment(const std::string& text,
                 std::vector<SemToken>& out) const;

    // Convenience overload
    std::vector<SemToken> segment(const std::string& text) const {
        std::vector<SemToken> r;
        segment(text, r);
        return r;
    }

    // ── Config ─────────────────────────────────────────────────────
    void set_config(const JiebaConfig& c) { config_ = c; }
    const JiebaConfig& config() const     { return config_; }

    // Reload with current config (calls shutdown + init internally).
    bool reload();

    // ── User dict ──────────────────────────────────────────────────
    // Load a custom user dict. Can be called between queries.
    // (cppjieba reloads its internal trie.)
    bool load_user_dict(const std::string& path);

private:
    JiebaConfig config_;
    std::unique_ptr<cppjieba::Jieba> jieba_;
};

} // namespace muji
