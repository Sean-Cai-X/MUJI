// src/3d/ISceneRenderer.h — 3D 场景渲染器纯虚接口
//
// 渲染器职责：拿 ISceneGraph3D (节点+边) + ICamera3D (相机)，
// 画到 ImGui ImDrawList 上。
//
// 第一个实现是 SoftwareRenderer3D —— 用 CPU 算 3D→2D 投影 + 深度排序。
// 后续可替换为 OpenGLRenderer (GPU)、VulkanRenderer 等。

#pragma once

#include "ISceneGraph3D.h"
#include "ICamera3D.h"
#include <imgui.h>
#include <vector>

namespace muji {
namespace scene3d {

struct RenderStats {
    int nodes_drawn = 0;
    int edges_drawn = 0;
    int nodes_visible = 0;
    float frame_ms = 0.0f;
};

class ISceneRenderer {
public:
    virtual ~ISceneRenderer() = default;

    // 把场景画到给定 ImGui 窗口的 DrawList 上
    virtual void render(ISceneGraph3D& graph,
                        ICamera3D& camera,
                        ImDrawList* drawlist,
                        const ImVec2& canvas_min,   // 画布左上角
                        const ImVec2& canvas_max,   // 画布右下角
                        RenderStats* stats = nullptr) = 0;

    // 命中测试 (返回屏幕位置下的最近节点 ID; -1 表示无)
    virtual int hit_test(ISceneGraph3D& graph,
                         ICamera3D& camera,
                         const Vec2& screen_pos) = 0;
};

} // namespace scene3d
} // namespace muji
