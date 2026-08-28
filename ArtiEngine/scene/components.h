#pragma once
#include "arti_renderer.h"

#include <cstdint>

#include <glm/vec3.hpp>

namespace arti::engine {

// 面向渲染的组件。ArtiChoco 的 scene 层刻意不认识渲染，所以它只提供 ID / Tag / Transform /
// Parent / WorldTransform 这些通用组件，这几个是 ArtiEngine 补的那一半。
//
// 这一步组件里直接存 rendering 的句柄。等资产层做起来之后会换成 AssetHandle<MeshAsset> 之类，
// 那时 extract 要多处理「资产还没加载完」—— 这是已知的一次返工，换来现在不必同时做两件难事。

struct MeshRendererComponent {
    rendering::MeshHandle mesh;
    rendering::MaterialHandle material;
    // 一个 Mesh 可以有多个 submesh，这里指定画哪一个。
    uint32_t submesh_index{ 0 };
    bool visible{ true };
};

// 没有 aspect 字段：宽高比是渲染目标的属性而不是相机的，由 extract 按目标尺寸算。
// 存一份在这里就会变成需要每帧同步的状态，而且会被序列化进场景文件 —— 在 21:9 存的场景
// 到 16:9 打开就是拉伸的。真需要覆盖（相机渲染到固定尺寸的离屏目标）再加 aspect_override。
struct CameraComponent {
    float fov_degrees{ 60.0f };
    float near_plane{ 0.1f };
    float far_plane{ 100.0f };
    // 一帧只用一个相机。多个都标 primary 时取遍历到的第一个。
    bool primary{ true };
};

// 没有 direction 字段：方向取实体世界变换的 -Z 轴，和 Unity / Godot 的惯例一致。
// 存一份方向就会和 Transform 的旋转打架，出现两个真值。
struct DirectionalLightComponent {
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity{ 1.0f };
    bool enabled{ true };
};

} // namespace arti::engine
