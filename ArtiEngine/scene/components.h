#pragma once
#include "arti_renderer.h"

#include <cstdint>

#include <glm/vec3.hpp>

namespace arti::engine {


struct MeshRendererComponent {
    rendering::MeshHandle mesh;
    rendering::MaterialHandle material;
    // 一个 Mesh 可以有多个 submesh，这里指定画哪一个。
    uint32_t submesh_index{ 0 };
    bool visible{ true };
};

struct CameraComponent {
    float fov_degrees{ 60.0f };
    float near_plane{ 0.1f };
    float far_plane{ 100.0f };
    // 一帧只用一个相机。多个都标 primary 时取遍历到的第一个。
    bool primary{ true };
};

struct DirectionalLightComponent {
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity{ 1.0f };
    bool enabled{ true };
};

} // namespace arti::engine
