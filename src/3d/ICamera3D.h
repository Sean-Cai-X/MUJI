// src/3d/ICamera3D.h — 3D 相机纯虚接口
//
// 相机定义了：位置、观察点、上方向、FOV、近远裁剪面。
// project() 把世界坐标 → 屏幕坐标 (像素)。
//
// SoftwareCamera3D (在 SoftwareRenderer3D 里实现) 是第一个实现类。
// 后续接入真实 3D 引擎时，只需实现同样接口的包装类。

#pragma once

#include "SceneTypes.h"

namespace muji {
namespace scene3d {

class ICamera3D {
public:
    virtual ~ICamera3D() = default;

    // ── 参数访问 ──────────────────────────────────────────────────
    virtual const Vec3& position()  const = 0;
    virtual const Vec3& look_at()   const = 0;
    virtual const Vec3& up()        const = 0;
    virtual float       fov_y()     const = 0;  // 垂直 FOV (弧度)
    virtual float       aspect()    const = 0;  // 宽高比
    virtual float       near_plane() const = 0;
    virtual float       far_plane()  const = 0;

    // ── 参数设置 ──────────────────────────────────────────────────
    virtual void set_position(const Vec3& p)  = 0;
    virtual void set_look_at(const Vec3& p)   = 0;
    virtual void set_up(const Vec3& p)        = 0;
    virtual void set_fov_y(float rad)         = 0;
    virtual void set_viewport_size(int w, int h) = 0;

    // ── 核心：世界坐标 → 屏幕坐标 ──────────────────────────────────
    // 返回 ProjectedPoint，包含屏幕像素坐标 + 深度 + 是否可见
    virtual ProjectedPoint project(const Vec3& world_pos) const = 0;

    // ── 旋转控制 (鼠标交互) ────────────────────────────────────────
    // yaw = 绕 Y 轴角度 (弧度), pitch = 绕 X 轴角度
    virtual void set_yaw_pitch(float yaw, float pitch) = 0;
    virtual void orbit_delta(float dyaw, float dpitch)  = 0;  // 拖拽增量
    virtual void dolly(float distance)                   = 0;  // 滚轮缩放
};

} // namespace scene3d
} // namespace muji
