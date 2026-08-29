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
class TextureAsset;
}

struct MeshRendererComponent {
    arti::asset::AssetHandle<asset::MeshAsset> mesh;
    std::vector<arti::asset::AssetHandle<asset::MaterialAsset>> materials;

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

struct EnvironmentComponent {
    arti::asset::AssetHandle<asset::TextureAsset> equirect_texture;
    glm::vec3 sky_color{ 0.03f, 0.03f, 0.035f };
    float intensity{ 1.0f };
    bool enabled{ true };
    bool sky_visible{ true };
};

}
