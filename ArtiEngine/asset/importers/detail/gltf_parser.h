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

// glTF 引用的一张外部图片，以及它在材质里的用途。
struct GltfImageUsage {
    std::filesystem::path file;  // 绝对路径
    // "base_color" / "emissive" / "normal" / "metallic_roughness" / "occlusion"
    std::string usage;
    // 颜色数据（base_color / emissive）需要 sRGB，数据贴图需要 linear。
    bool is_color{ false };
};

GltfScene parseGltf(const std::filesystem::path& source_file);

// 只解析 JSON 头，不加载 buffer、不解码图片：列出被引用的外部图片及其用途。
// 内嵌图片（data URI / buffer view）没有源文件，不在这里出现 —— 它们仍然由
// GltfImporter 产出成子资产。
std::vector<GltfImageUsage> prescanGltfImages(const std::filesystem::path& source_file);

}
