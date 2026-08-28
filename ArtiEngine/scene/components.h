#pragma once
#include "arti_renderer.h"
#include "artichoco/asset/asset.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace arti::engine {

namespace asset {
class MaterialAsset;
class MeshAsset;
} // namespace asset

struct MeshRendererComponent {
    arti::asset::AssetHandle<asset::MeshAsset> mesh;
    std::vector<arti::asset::AssetHandle<asset::MaterialAsset>> materials;

    uint32_t submesh_index{ 0 };
    bool visible{ true };
};

struct CameraComponent {
    float fov_degrees{ 60.0f };
    float near_plane{ 0.1f };
    float far_plane{ 100.0f };
    bool primary{ true };
};

struct DirectionalLightComponent {
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity{ 1.0f };
    bool enabled{ true };
};

} // namespace arti::engine
