// src/3d/SemanticSceneBuilder.h — 把 MUJI 7 层语义数据 → 3D 场景图
//
// 核心设计: z 轴 = 语义深度层
//   z = layer * LAYER_SPACING
//   layer 0 (jieba tokens)     → z = 0
//   layer 1 (wordnet synsets)  → z = -3
//   layer 2 (stardict)         → z = -6
//   layer 3 (kiwix entities)   → z = -9
//   layer 4 (vector)           → z = -12
//   layer 5 (lan-agent route)  → z = -15
//
// 同一 layer 内节点在 x-y 平面上做力导向布局 (简化版: 按顺序排成环)
//
// 边 = 语义关系:
//   - jieba token → wordnet synset        = Defines (候选)
//   - wordnet synset → wordnet synset     = IsA (上下位)
//   - stardict token → stardict definition = Defines
//   - kiwix entity → wordnet synset        = IsA (实体实例)
//   - lan-agent route → 目标 tool         = RoutesTo
//   - vector → 语义节点                    = Embedding
//
// 用法:
//   auto graph = std::make_unique<SceneGraph3D>();
//   SemanticSceneBuilder builder;
//   builder.build_from_pipeline(*graph, bus);  // 从 SemanticBus 构建
//   然后 renderer.render(*graph, camera, drawlist, ...);

#pragma once

#include "ISceneGraph3D.h"
#include "../glue/SemanticBus.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace muji {
namespace scene3d {

class SceneGraph3D;  // concrete impl — 见下方

class SemanticSceneBuilder {
public:
    SemanticSceneBuilder() = default;

    // 从 SemanticBus 的 active_context 构建 3D 场景
    // 这是主入口 —— 把 pipeline 已经跑完的所有层数据灌进场景
    void build_from_pipeline(SceneGraph3D& graph, SemanticBus& bus);

    // 手动添加一个语义节点 (调试用)
    void add_manual_node(SceneGraph3D& graph,
                         const std::string& label,
                         SemanticLayer layer,
                         float x = 0, float y = 0, float z = 0,
                         const std::string& source_tag = "manual");

    // 清除场景并重建 (下次 pipeline 跑完可调用)
    void rebuild(SceneGraph3D& graph, SemanticBus& bus);  // 实现在 .cpp

    // ── 布局参数 ──────────────────────────────────────────────
    float layer_spacing = 3.0f;    // 每层 z 间隔
    float node_radius   = 0.15f;   // 默认节点世界半径
    float ring_radius   = 2.0f;    // 同层节点环半径

    // 每个 layer 的 z 值
    float layer_z(SemanticLayer l) const {
        return -(int)l * layer_spacing;
    }

private:
    // 同层 N 个节点均匀分布在环上 (x-y 平面)
    Vec3 ring_position(int index, int count, SemanticLayer layer) const;

    uint32_t add_edge_safe(SceneGraph3D& graph, uint32_t from, uint32_t to,
                           EdgeType type, const std::string& label = "",
                           float weight = 1.0f);
};

// ── SceneGraph3D: ISceneGraph3D 的具体实现 ─────────────────────────────
class SceneGraph3D : public ISceneGraph3D {
public:
    uint32_t add_node(const SceneNode& node) override {
        uint32_t id = next_node_id_++;
        SceneNode copy = node;
        copy.id = id;
        nodes_.push_back(copy);
        return id;
    }

    bool update_node(uint32_t id, const SceneNode& node) override {
        for (auto& n : nodes_)
            if (n.id == id) { n = node; n.id = id; return true; }
        return false;
    }

    bool remove_node(uint32_t id) override {
        auto it = std::remove_if(nodes_.begin(), nodes_.end(),
                                 [id](const SceneNode& n) { return n.id == id; });
        if (it != nodes_.end()) {
            nodes_.erase(it, nodes_.end());
            // 同时移除相关边
            edges_.erase(
                std::remove_if(edges_.begin(), edges_.end(),
                               [id](const SceneEdge& e) {
                                   return e.from == id || e.to == id;
                               }),
                edges_.end());
            return true;
        }
        return false;
    }

    const SceneNode* get_node(uint32_t id) const override {
        for (auto& n : nodes_) if (n.id == id) return &n;
        return nullptr;
    }
    SceneNode* get_node_mut(uint32_t id) override {
        for (auto& n : nodes_) if (n.id == id) return &n;
        return nullptr;
    }

    uint32_t add_edge(const SceneEdge& edge) override {
        uint32_t id = next_edge_id_++;
        SceneEdge copy = edge;
        copy.id = id;
        edges_.push_back(copy);
        return id;
    }
    bool remove_edge(uint32_t id) override {
        auto it = std::remove_if(edges_.begin(), edges_.end(),
                                 [id](const SceneEdge& e) { return e.id == id; });
        if (it != edges_.end()) { edges_.erase(it, edges_.end()); return true; }
        return false;
    }

    const std::vector<SceneNode>& nodes() const override { return nodes_; }
    const std::vector<SceneEdge>& edges() const override { return edges_; }
    size_t node_count() const override { return nodes_.size(); }
    size_t edge_count() const override { return edges_.size(); }

    void clear() override {
        nodes_.clear(); edges_.clear();
        next_node_id_ = 1; next_edge_id_ = 1;
    }
    void reserve(size_t n, size_t e) override {
        nodes_.reserve(n); edges_.reserve(e);
    }

private:
    std::vector<SceneNode> nodes_;
    std::vector<SceneEdge> edges_;
    uint32_t next_node_id_ = 1;
    uint32_t next_edge_id_ = 1;
};

} // namespace scene3d
} // namespace muji
