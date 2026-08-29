#pragma once
#include "arti_renderer.h"
#include "asset/prefab_asset.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace arti::engine::asset::detail {

struct GltfImage {
    std::string key;
    std::filesystem::path file;
    std::vector<std::byte> bytes;
};

struct GltfMaterial {
    std::string name;
    int base_color_image{ -1 };
    int metallic_roughness_image{ -1 };
    int normal_image{ -1 };
    int occlusion_image{ -1 };
    int emissive_image{ -1 };

    glm::vec4 base_color{ 1.0f, 1.0f, 1.0f, 1.0f };
    float metallic{ 1.0f };
    float roughness{ 1.0f };
    float occlusion{ 1.0f };
    float emissive{ 0.0f };
};

struct GltfMesh {
    std::string name;
    std::vector<rendering::MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<rendering::Submesh> submeshes;
    std::vector<int> material_indices;
    std::vector<std::string> material_slots;
};

struct GltfNode {
    std::string name;
    glm::mat4 local_transform{ 1.0f };
    uint32_t parent{ kNoParentNode };
    int mesh{ -1 };
};

struct GltfScene {
    std::vector<GltfImage> images;
    std::vector<GltfMaterial> materials;
    std::vector<GltfMesh> meshes;
    std::vector<GltfNode> nodes;
};

GltfScene parseGltf(const std::filesystem::path& source_file);

}
