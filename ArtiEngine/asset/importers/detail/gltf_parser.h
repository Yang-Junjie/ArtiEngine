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

// cgltf 的数据结构翻译成引擎中立的形状。GltfImporter 只跟这一层打交道 ——
// cgltf 的所有权（cgltf_free）、accessor 的 componentType × normalized 组合、
// GLB 的 buffer view 偏移，这些细节都关在 .cpp 里。
//
// ObjImporter 是直接用 tinyobj 的，没有这一层；差别在于 tinyobj 解析完就是普通 vector，
// 而 cgltf 是一张需要手动释放的指针图，混在 importer 里会让那个函数变得很难读。

// 一张图片。按 URI（外部文件）或 image 序号（内嵌）去重之后的结果。
struct GltfImage {
    // 去重键，同时用作子资产的 suffix，所以必须在同一个 glTF 里唯一且稳定。
    std::string key;
    // 外部文件的绝对路径。为空表示图片内嵌，数据在 bytes 里。
    std::filesystem::path file;
    // 内嵌图片的编码字节（PNG / JPG 的原始内容，不是解码后的像素）。
    std::vector<std::byte> bytes;
};

// 一个 glTF 材质。贴图用 images 的下标，-1 表示该槽位为空。
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
    // emissiveFactor 的亮度 × KHR_materials_emissive_strength。见 .cpp 里关于有损映射的说明。
    float emissive{ 0.0f };
};

// 一个 glTF mesh，**局部（bind）空间**。primitive 一对一映射成 submesh。
//
// 顶点不做节点变换：MeshAsset 是可复用的资产，同一个 mesh 可能被多个节点以不同变换引用。
// 变换属于 PrefabNode。
struct GltfMesh {
    std::string name;
    std::vector<rendering::MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<rendering::Submesh> submeshes;
    // 每个 primitive 一个，值是 materials 的下标，-1 表示该 primitive 没有材质。
    std::vector<int> material_indices;
    // 每个 primitive 一个，材质名（或 "slot<n>"），进 mesh artifact 的槽名表。
    std::vector<std::string> material_slots;
};

// 一个 glTF 节点。列表是**前序**的，所以 parent 的下标一定小于自己 —— PrefabAsset
// 要求拓扑有序，这个顺序直接满足它。
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

// 解析 .gltf / .glb。外部 buffer 和外部图片按 source_file 的所在目录解析。
// 失败抛 std::runtime_error，消息里带上 cgltf 的具体原因。
GltfScene parseGltf(const std::filesystem::path& source_file);

} // namespace arti::engine::asset::detail
