// src/3d/SoftwareRenderer3D.h — CPU 3D→2D 投影渲染器 + 3D 相机实现
//
// 真 3D 数学 (不是伪 3D 渐变阴影):
//   world_pos → view matrix (旋转+平移) → perspective matrix → NDC → screen
// 用 ImGui ImDrawList 画圆 (节点) 和线 (边)。
// 按 depth 排序 (painter's algorithm) 做遮挡。
//
// 后续可替换为 OpenGL/Vulkan 渲染器 —— 只需实现同样的 ISceneRenderer 接口。

#pragma once

#include "ISceneRenderer.h"
#include "ISceneGraph3D.h"
#include "ICamera3D.h"
#include "SceneTypes.h"
#include <memory>

namespace muji {
namespace scene3d {

// ── SoftwareCamera3D ────────────────────────────────────────────────────
// 真 3D 相机实现: orbit  around look_at
class SoftwareCamera3D : public ICamera3D {
public:
    SoftwareCamera3D();

    const Vec3& position()    const override { return pos_; }
    const Vec3& look_at()     const override { return target_; }
    const Vec3& up()          const override { return up_; }
    float       fov_y()       const override { return fov_y_; }
    float       aspect()      const override { return aspect_; }
    float       near_plane()  const override { return near_; }
    float       far_plane()   const override { return far_; }

    void set_position(const Vec3& p)    override { pos_ = p; }
    void set_look_at(const Vec3& p)     override { target_ = p; }
    void set_up(const Vec3& p)           override { up_ = p.normalized(); }
    void set_fov_y(float rad)            override { fov_y_ = rad; }
    void set_viewport_size(int w, int h) override { aspect_ = (w > 0 && h > 0) ? (float)w / (float)h : 1.0f; vp_w_ = w; vp_h_ = h; }

    ProjectedPoint project(const Vec3& world_pos) const override;

    void set_yaw_pitch(float yaw, float pitch) override;
    void orbit_delta(float dyaw, float dpitch) override;
    void dolly(float distance)                  override;

    // 额外便利: 直接设置相机到 (distance, yaw, pitch) 的球面坐标
    void set_spherical(float distance, float yaw, float pitch);

    // 访问器 (用于调试)
    float yaw()   const { return yaw_; }
    float pitch() const { return pitch_; }
    float distance() const { return distance_; }

private:
    Vec3 pos_{0, 0, 8};
    Vec3 target_{0, 0, 0};
    Vec3 up_{0, 1, 0};
    float fov_y_   = 1.0f;   // ~57 度
    float aspect_  = 1.6f;
    float near_    = 0.1f;
    float far_     = 100.0f;
    int   vp_w_    = 800;
    int   vp_h_    = 500;

    // 球面坐标缓存
    float yaw_     = 0.0f;   // 绕 Y 轴
    float pitch_   = 0.3f;   // 仰角
    float distance_ = 8.0f;

    void rebuild_from_spherical();
    Mat4 compute_view_matrix() const;
    Mat4 compute_projection_matrix() const;
};

// ── SoftwareRenderer3D ──────────────────────────────────────────────────

class SoftwareRenderer3D : public ISceneRenderer {
public:
    SoftwareRenderer3D();

    void render(ISceneGraph3D& graph,
                ICamera3D& camera,
                ImDrawList* drawlist,
                const ImVec2& canvas_min,
                const ImVec2& canvas_max,
                RenderStats* stats = nullptr) override;

    int hit_test(ISceneGraph3D& graph,
                 ICamera3D& camera,
                 const Vec2& screen_pos) override;

private:
    struct DrawItem {
        float depth;         // 用于排序 (越大越远, painter's 先画远的)
        bool  is_edge;
        // edge fields
        uint32_t edge_id;
        Vec2 a_screen, b_screen;
        float edge_weight;
        Color edge_color;
        std::string edge_label;
        // node fields
        uint32_t node_id;
        Vec2  node_screen;
        float node_radius_px;
        Color node_color;
        std::string node_label;
        bool node_selected;
        bool node_hovered;
        NodeShape node_shape;
    };

    std::vector<DrawItem> items_;  // 重用, 避免每帧重新分配

    void build_draw_list(ISceneGraph3D& graph,
                         ICamera3D& camera,
                         const ImVec2& canvas_min,
                         const ImVec2& canvas_max);
};

// ── 便捷工厂 ───────────────────────────────────────────────────────────

inline std::unique_ptr<SoftwareCamera3D> make_camera() {
    return std::make_unique<SoftwareCamera3D>();
}

inline std::unique_ptr<SoftwareRenderer3D> make_renderer() {
    return std::make_unique<SoftwareRenderer3D>();
}

} // namespace scene3d
} // namespace muji
