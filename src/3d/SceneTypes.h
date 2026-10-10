// src/3d/SceneTypes.h — 3D scene fundamental types (zero external deps)
//
// 设计原则：这些是纯数据结构，不依赖任何 3D 引擎。
// 后续接入 OpenGL / Vulkan / OpenSceneGraph / bgfx 时直接复用。

#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace muji {
namespace scene3d {

// ── 基础数学类型 ──────────────────────────────────────────────────────

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3() = default;
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s)        const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s)        const { return {x / s, y / s, z / s}; }

    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const { float l = length(); return l > 1e-6f ? *this / l : Vec3{}; }

    float dot(const Vec3& o)  const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x };
    }
};

struct Vec2 {
    float x = 0.0f, y = 0.0f;
    Vec2() = default;
    Vec2(float a, float b) : x(a), y(b) {}
};

// 行优先 4x4 矩阵 (行主序，和 OpenGL glm 一致)
struct Mat4 {
    float m[4][4] = {
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 0, 1},
    };

    static Mat4 identity() { return Mat4{}; }

    // 绕 Y 轴旋转 (yaw)
    static Mat4 rotate_y(float radians) {
        Mat4 r;
        float c = std::cos(radians), s = std::sin(radians);
        r.m[0][0] =  c; r.m[0][2] =  s;
        r.m[2][0] = -s; r.m[2][2] =  c;
        return r;
    }

    // 绕 X 轴旋转 (pitch)
    static Mat4 rotate_x(float radians) {
        Mat4 r;
        float c = std::cos(radians), s = std::sin(radians);
        r.m[1][1] =  c; r.m[1][2] = -s;
        r.m[2][1] =  s; r.m[2][2] =  c;
        return r;
    }

    // 平移
    static Mat4 translate(float tx, float ty, float tz) {
        Mat4 r;
        r.m[0][3] = tx; r.m[1][3] = ty; r.m[2][3] = tz;
        return r;
    }

    // 透视投影 (FOV = vertical, aspect = w/h, near, far)
    static Mat4 perspective(float fov_y, float aspect, float near, float far) {
        Mat4 r{};
        float f = 1.0f / std::tan(fov_y * 0.5f);
        r.m[0][0] = f / aspect;
        r.m[1][1] = f;
        r.m[2][2] = (far + near) / (near - far);
        r.m[2][3] = (2.0f * far * near) / (near - far);
        r.m[3][2] = -1.0f;
        return r;
    }

    Vec3 transform(const Vec3& v) const {
        float w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
        return {
            (m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3]) / w,
            (m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3]) / w,
            (m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]) / w,
        };
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j]
                      + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
        }
    return r;
}

// ── 颜色 (RGBA, 0-255) ──────────────────────────────────────────────────

struct Color {
    uint8_t r = 200, g = 200, b = 220, a = 255;
    Color() = default;
    Color(uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255) : r(R), g(G), b(B), a(A) {}

    uint32_t to_imgui_u32() const {
        // ImGui uses 0xAABBGGRR (little-endian)
        return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
    }
};

// 按语义层给默认颜色
inline Color layer_color(int layer, float alpha = 1.0f) {
    // layer 0 = 最外层 (jieba), layer 5 = 最内层 (lan-agent)
    static const Color palette[] = {
        {100, 180, 255},   // 0 jieba      — 蓝
        {255, 180, 100},   // 1 wordnet    — 橙
        {120, 220, 140},   // 2 stardict   — 绿
        {255, 140, 200},   // 3 kiwix      — 粉
        {200, 160, 255},   // 4 vector     — 紫
        {255, 240, 100},   // 5 lan-agent  — 金
    };
    int idx = (layer >= 0 && layer < 6) ? layer : 0;
    Color c = palette[idx];
    c.a = (uint8_t)(255.0f * alpha);
    return c;
}

// ── SceneNode (语义节点) ───────────────────────────────────────────────

// 节点对应的语义数据来源
enum class SemanticLayer : int {
    Jieba     = 0,  // 分词 token
    WordNet   = 1,  // WordNet synset
    StarDict  = 2,  // StarDict / GoldenDict 词条
    Kiwix     = 3,  // Kwiki / Wiki 实体
    Vector    = 4,  // 可溯源向量
    LanAgent  = 5,  // lan-agent MCP 路由结果
};

// 节点类型 (控制渲染形状)
enum class NodeShape {
    Sphere,     // 概念节点
    Cube,       // 实体节点
    Cylinder,   // 路由节点
    Cone,       // 触发事件
};

struct SceneNode {
    uint32_t id = 0;               // 唯一 ID (自动分配)
    std::string label;              // 显示文本 (如 "NUMA", "memory.n.01")
    std::string source_tag;         // 来源适配器 (cppjieba, WordNet, StarDict...)
    SemanticLayer layer = SemanticLayer::Jieba;
    NodeShape shape = NodeShape::Sphere;

    Vec3 position;                  // 3D 世界坐标
    float radius = 0.15f;           // 节点半径 (world units)
    Color color;                    // 覆盖颜色 (默认按 layer_color)

    std::string semantic_id;        // 可选: 原始语义 ID (如 "n.01234567")
    std::string tooltip;            // 悬停提示文本
    std::unordered_map<std::string, std::string> metadata;  // KV 扩展

    // 渲染状态 (由 renderer 设置/查询)
    bool selected = false;
    bool hovered  = false;
};

// ── SceneEdge (语义边) ─────────────────────────────────────────────────

// 边类型 (语义关系)
enum class EdgeType {
    IsA,            // 上下位 (hypernym)
    PartOf,         // 部分-整体 (meronym)
    Defines,        // 定义关系 (词典词条)
    Synonym,        // 同义
    Translates,     // 翻译
    RoutesTo,       // MCP 路由决策
    Embedding,      // 向量相似度
    Custom,         // 自定义
};

struct SceneEdge {
    uint32_t id = 0;
    uint32_t from = 0;              // SceneNode.id
    uint32_t to   = 0;
    EdgeType type = EdgeType::Custom;
    std::string label;              // 可选关系标签
    float weight = 1.0f;            // 权重 (影响线宽/透明度)
    Color color;                    // 覆盖颜色
    bool directed = false;          // 是否有向
};

// ── 投影结果 ──────────────────────────────────────────────────────────

struct ProjectedPoint {
    Vec2 screen;        // 屏幕坐标 (像素)
    float depth = 0.0f; // Z 深度 (用于排序; 越大=越远)
    bool visible = true; // 是否在近远裁剪面内
};

struct ProjectedNode {
    const SceneNode* node = nullptr;
    ProjectedPoint projected;
    float screen_radius = 5.0f;  // 像素半径
};

} // namespace scene3d
} // namespace muji
