// src/3d/SemanticSceneBuilder.cpp — MUJI 7 层 → 3D 场景图
//
// 构建顺序 (z 轴从 0 → -15):
//   Layer 0: jieba tokens           → z = 0
//   Layer 1: wordnet synset 候选     → z = -3
//   Layer 2: Stardict/GoldenDict    → z = -6
//   Layer 3: Kiwix wiki 实体         → z = -9
//   Layer 4: TraceableVector         → z = -12
//   Layer 5: lan-agent MCP 路由       → z = -15
//
// 同层节点在 x-y 平面排成环 (ring_position)。
// 跨层边: token → synset (Defines),
//         token → definition (Defines), route → target (RoutesTo)
//
// TODO (后续): WordNetAdapter 需把 synset 关系 (hypernym/hyponym)
//              写入 SemanticContext::synset_relations, 才能画 IsA 边。

#include "SemanticSceneBuilder.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace muji {
namespace scene3d {

// SourceTag → 短字符串
static const char* source_to_str(muji::SourceTag s) {
    switch (s) {
        case muji::SourceTag::JiebaToken:    return "cppjieba";
        case muji::SourceTag::WordNetSynset: return "WordNet";
        case muji::SourceTag::GoldenDict:   return "GoldenDict";
        case muji::SourceTag::StarDict:      return "StarDict";
        case muji::SourceTag::KiwixZIM:      return "Kiwix";
        case muji::SourceTag::VectorDerived: return "Vector";
        case muji::SourceTag::LanAgentMCP:  return "LanAgentMCP";
        default:                             return "unknown";
    }
}

Vec3 SemanticSceneBuilder::ring_position(int index, int count, SemanticLayer layer) const {
    if (count <= 0) count = 1;
    float angle = (float)index / (float)count * 2.0f * 3.14159265f;
    float x = std::cos(angle) * ring_radius;
    float y = std::sin(angle) * ring_radius;
    float z = layer_z(layer);
    return {x, y, z};
}

uint32_t SemanticSceneBuilder::add_edge_safe(SceneGraph3D& graph,
                                             uint32_t from, uint32_t to,
                                             EdgeType type, const std::string& label,
                                             float weight) {
    SceneEdge e;
    e.from = from; e.to = to;
    e.type = type;
    e.label = label;
    e.weight = weight;
    e.directed = true;
    switch (type) {
        case EdgeType::IsA:       e.color = {255, 180, 100, 180}; break;
        case EdgeType::Defines:   e.color = {120, 220, 140, 180}; break;
        case EdgeType::RoutesTo:  e.color = {255, 240, 100, 200}; break;
        case EdgeType::Embedding: e.color = {200, 160, 255, 160}; break;
        default:                  e.color = {200, 200, 200, 140};
    }
    return graph.add_edge(e);
}

void SemanticSceneBuilder::add_manual_node(SceneGraph3D& graph,
                                           const std::string& label,
                                           SemanticLayer layer,
                                           float x, float y, float z,
                                           const std::string& source_tag) {
    SceneNode n;
    n.label = label;
    n.layer = layer;
    n.source_tag = source_tag;
    n.position = {x, y, z};
    n.radius = node_radius;
    n.color = layer_color((int)layer);
    graph.add_node(n);
}

void SemanticSceneBuilder::rebuild(SceneGraph3D& graph, SemanticBus& bus) {
    graph.clear();
    build_from_pipeline(graph, bus);
}

void SemanticSceneBuilder::build_from_pipeline(SceneGraph3D& graph,
                                               SemanticBus& bus) {
    graph.clear();
    graph.reserve(128, 256);

    auto& ctx = bus.active_context();

    std::unordered_map<std::string, uint32_t> id_map;
    auto key = [](SemanticLayer l, const std::string& k) {
        return std::to_string((int)l) + ":" + k;
    };

    // ═══ Layer 0: jieba tokens ═══
    {
        size_t n = ctx.tokens.size();
        for (size_t i = 0; i < n; ++i) {
            auto& t = ctx.tokens[i];
            SceneNode node;
            node.label = t.word;
            if (!t.pos.empty()) node.tooltip = "pos=" + t.pos;
            node.layer = SemanticLayer::Jieba;
            node.source_tag = "cppjieba";
            node.position = ring_position((int)i, (int)n, SemanticLayer::Jieba);
            node.radius = node_radius;
            node.color = layer_color(0);
            auto id = graph.add_node(node);
            id_map[key(SemanticLayer::Jieba, t.word)] = id;
        }
    }

    // ═══ Layer 1: WordNet synset 候选 ═══
    {
        int total = 0;
        for (auto& [w, ss] : ctx.synset_candidates) total += (int)ss.size();
        total = std::max(total, 1);

        int idx = 0;
        for (auto& [word, synsets] : ctx.synset_candidates) {
            auto token_it = id_map.find(key(SemanticLayer::Jieba, word));

            for (auto& s : synsets) {
                SceneNode node;
                // 用第一个 lemma 做 label, 否则 synset_id
                if (!s.lemmas.empty()) node.label = s.lemmas[0];
                else node.label = s.synset_id;
                node.semantic_id = s.synset_id;
                if (!s.definition.empty()) node.tooltip = s.definition;
                node.layer = SemanticLayer::WordNet;
                node.source_tag = "WordNet";
                node.position = ring_position(idx++, total, SemanticLayer::WordNet);
                node.radius = node_radius * 1.1f;
                node.color = layer_color(1);
                auto nid = graph.add_node(node);
                id_map[key(SemanticLayer::WordNet, s.synset_id)] = nid;

                if (token_it != id_map.end()) {
                    add_edge_safe(graph, token_it->second, nid,
                                  EdgeType::Defines, "synset candidate");
                }
            }
        }
    }

    // ═══ Layer 2: Dictionary definitions (StarDict/GoldenDict) ═══
    {
        int total = 0;
        for (auto& [w, es] : ctx.definitions) total += (int)es.size();
        total = std::max(total, 1);

        int idx = 0;
        for (auto& [word, entries] : ctx.definitions) {
            auto token_it = id_map.find(key(SemanticLayer::Jieba, word));

            for (auto& e : entries) {
                SceneNode node;
                node.label = e.word;     // DictEntry.word = 查询词
                if (!e.definition.empty()) {
                    size_t cap = std::min(e.definition.size(), (size_t)80);
                    node.tooltip = e.definition.substr(0, cap);
                }
                node.layer = SemanticLayer::StarDict;
                node.source_tag = e.db_name;   // 用词典名 (e.g. "XDICT英汉辞典")
                if (node.source_tag.empty()) node.source_tag = source_to_str(e.source);
                node.position = ring_position(idx++, total, SemanticLayer::StarDict);
                node.radius = node_radius * 0.9f;
                node.color = layer_color(2);
                auto nid = graph.add_node(node);
                id_map[key(SemanticLayer::StarDict,
                           e.word + "::" + node.source_tag)] = nid;

                if (token_it != id_map.end()) {
                    add_edge_safe(graph, token_it->second, nid,
                                  EdgeType::Defines, node.source_tag);
                }
            }
        }
    }

    // ═══ Layer 3: Kiwix wiki entities ═══
    {
        int total = 0;
        for (auto& [w, as] : ctx.zim_articles) total += (int)as.size();
        total = std::max(total, 1);

        int idx = 0;
        for (auto& [word, articles] : ctx.zim_articles) {
            auto token_it = id_map.find(key(SemanticLayer::Jieba, word));
            for (auto& a : articles) {
                SceneNode node;
                node.label = a.title;
                if (!a.content_preview.empty()) {
                    size_t cap = std::min(a.content_preview.size(), (size_t)80);
                    node.tooltip = a.content_preview.substr(0, cap);
                }
                node.layer = SemanticLayer::Kiwix;
                node.source_tag = "Kiwix";
                node.position = ring_position(idx++, total, SemanticLayer::Kiwix);
                node.shape = NodeShape::Cube;
                node.radius = node_radius * 1.2f;
                node.color = layer_color(3);
                auto nid = graph.add_node(node);

                if (token_it != id_map.end()) {
                    add_edge_safe(graph, token_it->second, nid,
                                  EdgeType::IsA, "wiki entity");
                }
            }
        }
    }

    // ═══ Layer 4: TraceableVector ═══
    {
        int idx = 0;
        int total = std::max((int)ctx.vectors.size(), 1);
        for (auto& [key_word, vec] : ctx.vectors) {
            auto token_it = id_map.find(key(SemanticLayer::Jieba, key_word));

            SceneNode node;
            node.label = "vec(" + key_word + ")";
            std::string dim_summary;
            int shown = 0;
            for (auto& label : vec.dim_labels) {
                if (shown++ >= 3) break;
                if (!dim_summary.empty()) dim_summary += ", ";
                dim_summary += label;
            }
            if (!dim_summary.empty()) node.tooltip = dim_summary;
            node.layer = SemanticLayer::Vector;
            node.source_tag = "Vector";
            node.position = ring_position(idx++, total, SemanticLayer::Vector);
            node.shape = NodeShape::Cylinder;
            node.radius = node_radius;
            node.color = layer_color(4);
            auto nid = graph.add_node(node);

            if (token_it != id_map.end()) {
                add_edge_safe(graph, token_it->second, nid,
                              EdgeType::Embedding, "vector");
            }
        }
    }

    // ═══ Layer 5: lan-agent MCP route ═══
    {
        auto& la = ctx.lan_agent;
        if (la.connected) {
            SceneNode gw;
            gw.label = "lan_agent_gateway";
            gw.tooltip = "endpoint=" + la.endpoint + " status=" + la.health_status;
            gw.layer = SemanticLayer::LanAgent;
            gw.source_tag = "LanAgentMCP";
            gw.position = {0, 0, layer_z(SemanticLayer::LanAgent)};
            gw.shape = NodeShape::Cylinder;
            gw.radius = node_radius * 1.5f;
            gw.color = layer_color(5);
            auto gw_id = graph.add_node(gw);

            for (auto& t : ctx.tokens) {
                auto it = id_map.find(key(SemanticLayer::Jieba, t.word));
                if (it != id_map.end()) {
                    add_edge_safe(graph, it->second, gw_id,
                                  EdgeType::RoutesTo, "MCP route");
                }
            }

            int idx = 0;
            int total = std::max((int)la.visible_tools.size(), 1);
            for (auto& tool_name : la.visible_tools) {
                SceneNode tool;
                tool.label = tool_name;
                tool.tooltip = "tool exposed via MCP gateway";
                tool.layer = SemanticLayer::LanAgent;
                tool.source_tag = "LanAgentMCP";
                tool.position = ring_position(idx++, total, SemanticLayer::LanAgent);
                tool.radius = node_radius;
                tool.color = {255, 200, 50, 255};
                auto tid = graph.add_node(tool);
                add_edge_safe(graph, gw_id, tid, EdgeType::RoutesTo, "exposes");
            }
        }
    }

    // 空 pipeline 占位
    if (graph.node_count() == 0) {
        SceneNode empty;
        empty.label = "(空 — pipeline 未运行)";
        empty.layer = SemanticLayer::Jieba;
        empty.source_tag = "system";
        empty.position = {0, 0, 0};
        empty.color = {100, 100, 100, 120};
        graph.add_node(empty);
    }
}

} // namespace scene3d
} // namespace muji
