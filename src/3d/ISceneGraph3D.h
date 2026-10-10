// src/3d/ISceneGraph3D.h — 3D 场景图纯虚接口
//
// 设计：SceneGraph 是"语义节点 + 语义边"的容器。
// 它不关心渲染 —— 渲染由 ISceneRenderer 完成。
// 数据源可以是任何东西（MUJI 7 层适配器的语义数据、外部导入、手工构建）。
//
// 关键：这是"假定接口" —— 不依赖任何 3D 库。
// 后续换用 OpenSceneGraph / Vulkan / bgfx 时，只需实现一个新的
// ISceneGraph3D 子类 + 配套 ISceneRenderer。

#pragma once

#include "SceneTypes.h"
#include <cstdint>
#include <string>
#include <vector>

namespace muji {
namespace scene3d {

class ISceneGraph3D {
public:
    virtual ~ISceneGraph3D() = default;

    // ── 节点操作 ──────────────────────────────────────────────────
    virtual uint32_t add_node(const SceneNode& node) = 0;
    virtual bool     update_node(uint32_t id, const SceneNode& node) = 0;
    virtual bool     remove_node(uint32_t id) = 0;
    virtual const SceneNode* get_node(uint32_t id) const = 0;
    virtual SceneNode*       get_node_mut(uint32_t id) = 0;

    // ── 边操作 ────────────────────────────────────────────────────
    virtual uint32_t add_edge(const SceneEdge& edge) = 0;
    virtual bool     remove_edge(uint32_t id) = 0;

    // ── 查询 ──────────────────────────────────────────────────────
    virtual const std::vector<SceneNode>& nodes() const = 0;
    virtual const std::vector<SceneEdge>& edges() const = 0;
    virtual size_t node_count() const = 0;
    virtual size_t edge_count() const = 0;

    // ── 批量操作 ──────────────────────────────────────────────────
    virtual void clear() = 0;
    virtual void reserve(size_t nodes, size_t edges) = 0;

    // ── 便捷方法 ──────────────────────────────────────────────────

    // 按 layer 返回节点子集
    std::vector<const SceneNode*> nodes_by_layer(SemanticLayer layer) const {
        std::vector<const SceneNode*> out;
        for (auto& n : nodes())
            if (n.layer == layer) out.push_back(&n);
        return out;
    }

    // 按 source_tag 返回节点子集
    std::vector<const SceneNode*> nodes_by_source(const std::string& source_tag) const {
        std::vector<const SceneNode*> out;
        for (auto& n : nodes())
            if (n.source_tag == source_tag) out.push_back(&n);
        return out;
    }

    // 查找与给定节点相连的所有边
    std::vector<const SceneEdge*> edges_for_node(uint32_t node_id) const {
        std::vector<const SceneEdge*> out;
        for (auto& e : edges())
            if (e.from == node_id || e.to == node_id) out.push_back(&e);
        return out;
    }
};

} // namespace scene3d
} // namespace muji
