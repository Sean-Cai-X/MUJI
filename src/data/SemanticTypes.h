// src/data/SemanticTypes.h
// Standard semantic data structures used across all MUJI glue adapters.
// Every native module (cppjieba, WordNet, GoldenDict, Kwiki, vector engine)
// MUST translate its own output into these types before handing up.
//
// Design rules:
//   - Value types only (no pointer ownership, no polymorphic base)
//   - No external library deps (no ImGui, no Eigen, no JSON)
//   - Small, serializable, copyable
//   - Traceable: every node/relation carries a SourceTag so downstream
//     can answer "where did this come from?"

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace muji {

// ── Source tag ──────────────────────────────────────────────────────
// Every data element carries a source tag so the vector layer can
// decompose similarity scores by origin (WordNet axiom? Wiki link?).
enum class SourceTag : uint8_t {
    Unknown        = 0,
    JiebaToken     = 1,   // layer 0 — segmentation
    JiebaUserDict  = 2,   // layer 0 — hit custom dict
    WordNetSynset  = 10,  // layer 1 — WordNet synset
    WordNetRel     = 11,  // layer 1 — WordNet axiom relation
    GoldenDict     = 20,  // layer 2 — dictd RFC 2229
    StarDict       = 21,  // layer 2b — StarDict .ifo/.idx/.dict
    KiwixZIM       = 22,  // layer 3 — ZIM offline article
    WikiEntity     = 30,  // layer 3 — Wiki entity
    WikiCategory   = 31,  // layer 3 — Wiki category
    WikiLink       = 32,  // layer 3 — Wiki link between entities
    VectorDerived  = 40,  // layer 4 — computed in vector engine
    LanAgentMCP    = 50,  // bridge   — codex-lan-agent MCP service (HTTP JSON-RPC)
};

inline const char* to_cstr(SourceTag t) {
    switch (t) {
        case SourceTag::Unknown:       return "Unknown";
        case SourceTag::JiebaToken:    return "JiebaToken";
        case SourceTag::JiebaUserDict: return "JiebaUserDict";
        case SourceTag::WordNetSynset: return "WordNetSynset";
        case SourceTag::WordNetRel:    return "WordNetRel";
        case SourceTag::GoldenDict:    return "GoldenDict";
        case SourceTag::StarDict:      return "StarDict";
        case SourceTag::KiwixZIM:      return "KiwixZIM";
        case SourceTag::WikiEntity:    return "WikiEntity";
        case SourceTag::WikiCategory:  return "WikiCategory";
        case SourceTag::WikiLink:      return "WikiLink";
        case SourceTag::VectorDerived: return "VectorDerived";
        case SourceTag::LanAgentMCP:   return "LanAgentMCP";
    }
    return "?";
}

// ── Layer 2: Dictionary entry (shared by GoldenDict + StarDict) ─────
struct DictEntry {
    std::string database;    // e.g. "wn", "gcide", "xdict-ec-gb"
    std::string db_name;     // e.g. "WordNet (r) 3.0", "XDICT英汉辞典"
    std::string word;        // queried word
    std::string definition;  // raw definition text (UTF-8)
    SourceTag   source = SourceTag::GoldenDict;
};

// ── Layer 0: Jieba token ────────────────────────────────────────────
struct SemToken {
    std::string word;           // 词形 e.g. "NUMA", "内存"
    std::string pos;            // 词性 e.g. "n", "v", "eng"
    int32_t     start = 0;      // 原文起始偏移
    int32_t     end   = 0;      // 原文结束偏移
    bool        is_oov = false; // 未登录词
    bool        hit_user_dict = false; // 命中自定义词典
    SourceTag   source = SourceTag::JiebaToken;
};

// ── Layer 1: WordNet synset ────────────────────────────────────────
enum class WordNetPos : uint8_t { Noun = 1, Verb = 2, Adj = 3, Adv = 4, Unknown = 0 };

struct SynsetNode {
    std::string synset_id;      // 唯一 ID e.g. "03790840-n"
    WordNetPos  wn_pos = WordNetPos::Unknown;
    std::string definition;     // WordNet 英文定义
    std::vector<std::string> lemmas; // 同义词集
    std::string zh_candidate;   // 可能的中文候选（来自 GoldenDict 对齐）
    SourceTag   source = SourceTag::WordNetSynset;
};

enum class WordNetRelType : uint8_t {
    Hypernym   = 1,  // 上位
    Hyponym    = 2,  // 下位
    Synonym    = 3,  // 同义
    Antonym    = 4,  // 反义
    Meronym    = 5,  // 部分
    Holonym    = 6,  // 整体
    Derivation = 7,  // 派生
    Instance   = 8,  // 实例
    Member     = 9,  // 成员
    Unknown    = 0,
};

struct WordNetRelation {
    std::string from_synset;
    std::string to_synset;
    WordNetRelType rel = WordNetRelType::Unknown;
    SourceTag   source = SourceTag::WordNetRel;
};

// ── Layer 2: Entity ────────────────────────────────────────────────
struct EntityNode {
    std::string entity_id;      // Wiki/Wikidata ID e.g. "Q12345"
    std::string title;          // "NUMA"
    std::string description;    // 简述
    std::vector<std::string> aliases;   // 别名
    std::vector<std::string> categories; // Wiki 分类
    double      disambig_score = 0.0;   // 消歧置信度 0~1
    SourceTag   source = SourceTag::WikiEntity;
};

struct EntityRelation {
    std::string from_entity;
    std::string to_entity;
    std::string predicate;      // e.g. "part_of", "belongs_to"
    SourceTag   source = SourceTag::WikiLink;
};

// ── Layer 3: Traceable vector ──────────────────────────────────────
// Each dimension of the vector carries a trace label, so similarity
// can be decomposed and explained.
struct TraceableVector {
    std::vector<float>                components;  // raw floats
    std::vector<std::string>          dim_labels;  // 每维语义标签 e.g. "hypernym:存储设备"
    std::vector<SourceTag>           dim_sources; // 每维来源
    std::unordered_map<std::string, float>
        weights_by_source;    // 按来源聚合权重（向量层内部用）
};

// Similarity result with trace — the whole point of the "mother-machine"
struct TraceableSimilarity {
    float total = 0.0f;                         // 总相似度
    std::vector<std::pair<std::string, float>>  // 拆解: (来源标签, 权重贡献)
        breakdown;
};

// ── Cross-layer context (for adapters to forward state) ─────────────
// A small context bag that flows between adapters. Each adapter writes
// its output into the relevant field; downstream adapters read.
struct SemanticContext {
    // Layer 0
    std::vector<SemToken> tokens;

    // Layer 1 (per-token candidate synsets — still ambiguous at this stage)
    std::unordered_map<std::string, std::vector<SynsetNode>> synset_candidates;

    // Layer 2: Dictionary definitions (GoldenDict dictd + StarDict)
    // key = word, value = all definitions from all loaded dicts
    std::unordered_map<std::string, std::vector<DictEntry>> definitions;

    // Layer 3a: ZIM articles (Kiwix) — key = word, value = found ZIM entries
    struct ZimArticle {
        std::string zim_name;
        std::string title;
        std::string path;
        std::string content_preview;  // first ~2KB HTML-stripped
        SourceTag   source = SourceTag::KiwixZIM;
    };
    std::unordered_map<std::string, std::vector<ZimArticle>> zim_articles;

    // Layer 2b (disambiguated entities — Kwiki next)
    std::vector<EntityNode> entities;
    std::vector<EntityRelation> entity_relations;

    // Layer 3 (vector engine — TraceableVector next)
    std::unordered_map<std::string, TraceableVector> vectors;

    // Bridge layer — codex-lan-agent MCP state & artifacts
    struct LanAgentState {
        bool        connected = false;
        std::string endpoint;          // e.g. "http://127.0.0.1:18080/mcp"
        std::string health_status;     // "success"/"failed"
        std::string workspace_root;
        std::string generation_endpoint;
        std::vector<std::string> visible_tools;
        std::string last_route_result; // 最近一次 mcp_route 返回的 tool name
        std::string last_raw_response; // 最近一次完整 JSON-RPC 原始响应 (调试用)
    };
    LanAgentState lan_agent;

    // Origin text (for debug display)
    std::string raw_text;
};

} // namespace muji
