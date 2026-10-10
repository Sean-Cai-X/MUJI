// src/adapters/WordNetAdapter.h
// Glue-layer adapter for WordNet 3.0 (layer 1 — lexical skeleton).
//
// Native entry point: wn command-line tool (system-installed), plus
// src/native/wn_cli.cpp (compiles independently, wraps the C API directly).
//
// This adapter:
//   - initializes libwordnet with data path (/usr/share/wordnet by default)
//   - wraps findtheinfo_ds / traceptrs_ds → SynsetNode + WordNetRelation
//   - does NOT modify WordNet internals

#pragma once

#include "glue/AbstractAdapter.h"
#include "data/SemanticTypes.h"

#include <string>
#include <vector>

namespace muji {

struct WordNetConfig {
    std::string dict_path = "/usr/share/wordnet";  // override if WordNet-3.0 elsewhere
};

class WordNetAdapter : public AbstractAdapter {
public:
    WordNetAdapter();
    ~WordNetAdapter() override;

    bool init() override;
    bool shutdown() override;

    void set_config(const WordNetConfig& c) { config_ = c; }
    const WordNetConfig& config() const    { return config_; }

    // ── Query: find synsets matching lemma (any POS, returns multiple) ──
    // Each returned SynsetNode is one sense of the lemma.
    std::vector<SynsetNode> lookup_synsets(const std::string& lemma) const;

    // ── Query: relations for a given synset (by synset_id) ──
    // Returns hypernyms (上位) by default; rel_type filterable.
    std::vector<WordNetRelation> get_relations(const std::string& synset_id,
                                               WordNetRelType rel) const;

    // ── Convenience: find hypernym tree (upward chain) ──
    std::vector<std::string> get_hypernyms(const std::string& lemma) const;

private:
    WordNetConfig config_;
    bool          initialized_ = false;
};

} // namespace muji
