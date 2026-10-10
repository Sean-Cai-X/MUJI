// src/3d/SoftwareRenderer3D.cpp — 真 3D CPU 投影 + ImDrawList 渲染
//
// 算法 (每帧):
//   1. 每个 node → project() → 屏幕坐标 + depth
//   2. 每个 edge → 两个 endpoint 各自 project() → 屏幕线段
//   3. 把 edge 和 node 放进同一个 items_ 数组
//   4. 按 depth 降序排序 (painter's: 远的先画)
//   5. drawlist->AddCircleFilled / AddLine / AddText

#include "SoftwareRenderer3D.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace muji {
namespace scene3d {

// ══════════════════════════════════════════════════════════════════════
// SoftwareCamera3D
// ══════════════════════════════════════════════════════════════════════

SoftwareCamera3D::SoftwareCamera3D() {
    rebuild_from_spherical();
}

void SoftwareCamera3D::set_spherical(float distance, float yaw, float pitch) {
    distance_ = std::max(0.5f, distance);
    yaw_      = yaw;
    pitch_    = std::clamp(pitch, -1.3f, 1.3f);  // 防止万向锁
    rebuild_from_spherical();
}

void SoftwareCamera3D::set_yaw_pitch(float yaw, float pitch) {
    set_spherical(distance_, yaw, pitch);
}

void SoftwareCamera3D::orbit_delta(float dyaw, float dpitch) {
    set_spherical(distance_, yaw_ + dyaw, pitch_ + dpitch);
}

void SoftwareCamera3D::dolly(float distance_delta) {
    set_spherical(distance_ + distance_delta, yaw_, pitch_);
}

void SoftwareCamera3D::rebuild_from_spherical() {
    // 世界坐标下的相机位置 (球面 → 笛卡尔)
    float cx = target_.x + distance_ * std::cos(pitch_) * std::sin(yaw_);
    float cy = target_.y + distance_ * std::sin(pitch_);
    float cz = target_.z + distance_ * std::cos(pitch_) * std::cos(yaw_);
    pos_ = {cx, cy, cz};
}

Mat4 SoftwareCamera3D::compute_view_matrix() const {
    // 手工 lookAt (无 glm 依赖)
    Vec3 f = (target_ - pos_).normalized();       // forward
    Vec3 s = f.cross(up_).normalized();            // right
    Vec3 u = s.cross(f).normalized();              // real up

    Mat4 view{};
    view.m[0][0] =  s.x; view.m[0][1] =  s.y; view.m[0][2] =  s.z; view.m[0][3] = -s.dot(pos_);
    view.m[1][0] =  u.x; view.m[1][1] =  u.y; view.m[1][2] =  u.z; view.m[1][3] = -u.dot(pos_);
    view.m[2][0] = -f.x; view.m[2][1] = -f.y; view.m[2][2] = -f.z; view.m[2][3] =  f.dot(pos_);
    view.m[3][0] =  0;   view.m[3][1] =  0;   view.m[3][2] =  0;   view.m[3][3] =  1;
    return view;
}

Mat4 SoftwareCamera3D::compute_projection_matrix() const {
    return Mat4::perspective(fov_y_, aspect_, near_, far_);
}

ProjectedPoint SoftwareCamera3D::project(const Vec3& world_pos) const {
    ProjectedPoint out;

    // world → view
    Mat4 vp = compute_projection_matrix() * compute_view_matrix();
    Vec3 clip = vp.transform(world_pos);

    // clip.w 已经在 Mat4::transform 里除了 (perspective divide)
    // clip.z 是 view-space depth (越大越远)
    float view_depth = (compute_view_matrix().transform(world_pos)).z;  // 原始 z

    // 近远裁剪
    out.depth = view_depth;
    out.visible = (clip.x >= -1.0f && clip.x <= 1.0f &&
                   clip.y >= -1.0f && clip.y <= 1.0f &&
                   view_depth > near_ && view_depth < far_);

    if (!out.visible) {
        out.screen = {-9999, -9999};
        return out;
    }

    // NDC → 屏幕
    float nx = clip.x, ny = clip.y;  // -1..1
    out.screen = {
        (nx + 1.0f) * 0.5f * (float)vp_w_,
        (1.0f - ny) * 0.5f * (float)vp_h_   // Y 翻转 (NDC +1 = 上, screen + = 下)
    };
    return out;
}

// ══════════════════════════════════════════════════════════════════════
// SoftwareRenderer3D
// ══════════════════════════════════════════════════════════════════════

SoftwareRenderer3D::SoftwareRenderer3D() {
    items_.reserve(512);
}

void SoftwareRenderer3D::render(ISceneGraph3D& graph,
                                 ICamera3D& camera,
                                 ImDrawList* drawlist,
                                 const ImVec2& canvas_min,
                                 const ImVec2& canvas_max,
                                 RenderStats* stats) {
    auto t0 = std::chrono::steady_clock::now();

    int w = (int)(canvas_max.x - canvas_min.x);
    int h = (int)(canvas_max.y - canvas_min.y);
    if (w <= 0 || h <= 0) return;

    // 让相机知道视口大小
    camera.set_viewport_size(w, h);

    build_draw_list(graph, camera, canvas_min, canvas_max);

    // painter's algorithm: 远的先画
    std::sort(items_.begin(), items_.end(),
              [](const DrawItem& a, const DrawItem& b) { return a.depth > b.depth; });

    // 画
    int edges_drawn = 0, nodes_drawn = 0;
    for (auto& item : items_) {
        if (item.is_edge) {
            float thickness = std::clamp(1.0f + item.edge_weight * 2.0f, 1.0f, 4.0f);
            drawlist->AddLine(
                {canvas_min.x + item.a_screen.x, canvas_min.y + item.a_screen.y},
                {canvas_min.x + item.b_screen.x, canvas_min.y + item.b_screen.y},
                item.edge_color.to_imgui_u32(), thickness);
            // 边标签 (可省略 — 太杂)
            // if (!item.edge_label.empty()) {
            //     auto mid = (item.a_screen + item.b_screen) / 2.0f;
            //     drawlist->AddText(
            //         {canvas_min.x + mid.x + 4, canvas_min.y + mid.y - 8},
            //         item.edge_color.to_imgui_u32(), item.edge_label.c_str());
            // }
            ++edges_drawn;
        } else {
            // 节点 (圆 + 标签)
            uint32_t col = item.node_color.to_imgui_u32();
            if (item.node_selected) {
                drawlist->AddCircleFilled(
                    {canvas_min.x + item.node_screen.x, canvas_min.y + item.node_screen.y},
                    item.node_radius_px + 3.0f,
                    IM_COL32(255, 255, 80, 120));  // 选中光环
            }
            drawlist->AddCircleFilled(
                {canvas_min.x + item.node_screen.x, canvas_min.y + item.node_screen.y},
                item.node_radius_px, col);
            // 描边
            drawlist->AddCircle(
                {canvas_min.x + item.node_screen.x, canvas_min.y + item.node_screen.y},
                item.node_radius_px + 1.0f, IM_COL32(255, 255, 255, 60));

            // 标签 (只对 radius >= 8 的节点显示, 否则太挤)
            if (item.node_radius_px >= 6.0f) {
                drawlist->AddText(
                    {canvas_min.x + item.node_screen.x + item.node_radius_px + 2.0f,
                     canvas_min.y + item.node_screen.y - 7.0f},
                    IM_COL32(255, 255, 255, 220),
                    item.node_label.c_str());
            }
            ++nodes_drawn;
        }
    }

    if (stats) {
        auto t1 = std::chrono::steady_clock::now();
        stats->edges_drawn = edges_drawn;
        stats->nodes_drawn = nodes_drawn;
        stats->nodes_visible = (int)items_.size();
        stats->frame_ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    }
}

int SoftwareRenderer3D::hit_test(ISceneGraph3D& graph,
                                  ICamera3D& camera,
                                  const Vec2& screen_pos) {
    int best_id = -1;
    float best_dist = 1e9f;

    for (auto& n : graph.nodes()) {
        auto p = camera.project(n.position);
        if (!p.visible) continue;
        float dx = p.screen.x - screen_pos.x;
        float dy = p.screen.y - screen_pos.y;
        float d = std::sqrt(dx * dx + dy * dy);
        if (d < 15.0f && d < best_dist) {  // 15px 容差
            best_dist = d;
            best_id = (int)n.id;
        }
    }
    return best_id;
}

void SoftwareRenderer3D::build_draw_list(ISceneGraph3D& graph,
                                          ICamera3D& camera,
                                          const ImVec2& canvas_min,
                                          const ImVec2& canvas_max) {
    items_.clear();

    // 缓存每个 node 的投影结果 (边需要两端)
    std::unordered_map<uint32_t, ProjectedPoint> proj_cache;

    // 先投影所有 node
    for (auto& n : graph.nodes()) {
        proj_cache[n.id] = camera.project(n.position);
    }

    // 加边 (先画 — painter's 远边先画)
    for (auto& e : graph.edges()) {
        auto it_a = proj_cache.find(e.from);
        auto it_b = proj_cache.find(e.to);
        if (it_a == proj_cache.end() || it_b == proj_cache.end()) continue;
        auto& pa = it_a->second;
        auto& pb = it_b->second;
        if (!pa.visible && !pb.visible) continue;

        // 使用较远的 endpoint depth 作为排序基准
        float depth = std::max(pa.depth, pb.depth);

        DrawItem di{};
        di.is_edge = true;
        di.edge_id = e.id;
        di.a_screen = pa.screen;
        di.b_screen = pb.screen;
        di.edge_weight = e.weight;
        di.edge_color = (e.color.r == 0 && e.color.g == 0 && e.color.b == 0)
                       ? layer_color(2, 0.6f) : e.color;  // 默认边色
        di.edge_label = e.label;
        di.depth = depth;
        items_.push_back(di);
    }

    // 加节点
    for (auto& n : graph.nodes()) {
        auto it = proj_cache.find(n.id);
        if (it == proj_cache.end()) continue;
        auto& p = it->second;
        if (!p.visible) continue;

        // 屏幕空间半径 — 简单 heuristic: 世界半径 * fov * distance
        // 简化: 用固定像素范围, 根据 depth 做轻微缩放
        float scale_hint = std::clamp(6.0f / std::max(p.depth, 0.5f), 0.3f, 1.5f);
        float screen_r = std::max(3.0f, n.radius * 40.0f * scale_hint);

        DrawItem di{};
        di.is_edge = false;
        di.node_id = n.id;
        di.node_screen = p.screen;
        di.node_radius_px = screen_r;
        di.node_color = (n.color.r == 0 && n.color.g == 0 && n.color.b == 0)
                       ? layer_color((int)n.layer) : n.color;
        di.node_label = n.label;
        di.node_selected = n.selected;
        di.node_hovered  = n.hovered;
        di.node_shape = n.shape;
        di.depth = p.depth;
        items_.push_back(di);
    }
}

} // namespace scene3d
} // namespace muji
