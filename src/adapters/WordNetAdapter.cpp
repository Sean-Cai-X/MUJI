// src/adapters/WordNetAdapter.cpp

#include "WordNetAdapter.h"

#include <wn.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace muji {

// Map libwn POS → WordNetPos
static WordNetPos libwn_to_pos(const char* pos) {
    if (!pos || !*pos) return WordNetPos::Unknown;
    switch (pos[0]) {
        case 'n': return WordNetPos::Noun;
        case 'v': return WordNetPos::Verb;
        case 'a': return WordNetPos::Adj;
        case 'r': return WordNetPos::Adv;
        default:  return WordNetPos::Unknown;
    }
}

// Map AGENTS.md WordNetRelType → libwn ptr constant
static int rel_to_ptr(WordNetRelType r) {
    switch (r) {
        case WordNetRelType::Hypernym:   return HYPERPTR;  // @
        case WordNetRelType::Hyponym:    return HYPOPTR;   // ~
        case WordNetRelType::Synonym:    return SYNS;      // same synset
        case WordNetRelType::Antonym:    return ANTPTR;    // !
        case WordNetRelType::Meronym:    return MERONYM;   // %
        case WordNetRelType::Holonym:    return HOLONYM;   // #
        case WordNetRelType::Derivation: return DERIVATION; // +
        default:                         return -1;
    }
}

WordNetAdapter::WordNetAdapter() : AbstractAdapter("WordNet") {}
WordNetAdapter::~WordNetAdapter() { shutdown(); }

bool WordNetAdapter::init() {
    if (initialized_) return true;

    // Point libwn at the WordNet database
    setenv("WNSEARCHPATH", config_.dict_path.c_str(), 1);

    if (wninit() != 0) {
        set_last_error("wninit() failed — check WordNet data path: " + config_.dict_path);
        return false;
    }

    initialized_ = true;
    set_status(AdapterStatus::Ready);
    return true;
}

bool WordNetAdapter::shutdown() {
    if (initialized_) {
        // libwn doesn't have an explicit close; reset flag only
        initialized_ = false;
    }
    set_status(AdapterStatus::Shutdown);
    return true;
}

std::vector<SynsetNode> WordNetAdapter::lookup_synsets(const std::string& lemma) const {
    std::vector<SynsetNode> out;
    if (!initialized_ || lemma.empty()) return out;

    // Try all 4 POS. For each, find all senses.
    static const int poses[] = { NOUN, VERB, ADJ, ADV };

    for (int p : poses) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", lemma.c_str());

        // findtheinfo_ds returns all senses as linked list via ->nextss
        SynsetPtr first = findtheinfo_ds(buf, p, SYNS, ALLSENSES);
        if (!first) continue;

        for (SynsetPtr s = first; s != nullptr; s = s->nextss) {
            SynsetNode node;
            // synset_id = hereiam offset + pos char, e.g. "12345-n"
            std::snprintf(buf, sizeof(buf), "%ld-%c", s->hereiam, s->pos[0]);
            node.synset_id = buf;
            node.wn_pos = libwn_to_pos(s->pos);
            node.definition = s->defn ? s->defn : "";
            // lemmas = words array
            if (s->words) {
                node.lemmas.reserve(s->wcount);
                for (int i = 0; i < s->wcount; ++i) {
                    if (s->words[i]) node.lemmas.push_back(s->words[i]);
                }
            }
            node.source = SourceTag::WordNetSynset;
            out.push_back(std::move(node));
        }
        free_syns(first);
    }
    return out;
}

std::vector<WordNetRelation> WordNetAdapter::get_relations(
    const std::string& synset_id, WordNetRelType rel) const {

    std::vector<WordNetRelation> out;
    if (!initialized_) return out;

    int ptr_type = rel_to_ptr(rel);
    if (ptr_type < 0) return out;

    // Parse synset_id "12345-n" → offset + pos
    long offset = 0;
    char pos_c = 'n';
    if (std::sscanf(synset_id.c_str(), "%ld-%c", &offset, &pos_c) != 2) return out;
    int pos_code = (pos_c == 'n') ? NOUN : (pos_c == 'v') ? VERB :
                   (pos_c == 'a') ? ADJ  : (pos_c == 'r') ? ADV : ALL_POS;

    // Read synset directly from offset
    SynsetPtr s = read_synset(pos_code, offset, nullptr);
    if (!s) return out;

    // Use traceptrs_ds to traverse the requested pointer type
    // depth=1 gives direct relations only
    SynsetPtr rels = traceptrs_ds(s, ptr_type, 1, 1);
    if (rels) {
        for (SynsetPtr r = rels; r != nullptr; r = r->ptrlist) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%ld-%c", r->hereiam, r->pos[0]);
            WordNetRelation wr;
            wr.from_synset = synset_id;
            wr.to_synset   = buf;
            wr.rel         = rel;
            wr.source      = SourceTag::WordNetRel;
            out.push_back(std::move(wr));
        }
        free_syns(rels);
    }

    free_synset(s);
    return out;
}

std::vector<std::string> WordNetAdapter::get_hypernyms(const std::string& lemma) const {
    std::vector<std::string> result;
    if (!initialized_) return result;

    auto synsets = lookup_synsets(lemma);
    for (auto& s : synsets) {
        auto rels = get_relations(s.synset_id, WordNetRelType::Hypernym);
        for (auto& r : rels) {
            result.push_back(r.to_synset);
        }
    }
    return result;
}

} // namespace muji
