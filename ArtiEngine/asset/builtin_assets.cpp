#include "asset/builtin_assets.h"

#include "artichoco/asset/asset_manager.h"
#include "asset/detail/mesh_artifact.h"
#include "asset/loaders/material_loader.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace arti::engine::asset {
namespace {

struct MeshData {
    std::vector<rendering::MeshVertex> vertices;
    std::vector<uint32_t> indices;
};

MeshData makeCube() {
    struct FaceDesc {
        glm::vec3 normal;
        glm::vec3 origin;
        glm::vec3 right;
        glm::vec3 up;
    };

    constexpr std::array<FaceDesc, 6> faces{ {
        { { 1, 0, 0 }, { 1, -1, 1 }, { 0, 0, -2 }, { 0, 2, 0 } },
        { { -1, 0, 0 }, { -1, -1, -1 }, { 0, 0, 2 }, { 0, 2, 0 } },
        { { 0, 1, 0 }, { -1, 1, 1 }, { 2, 0, 0 }, { 0, 0, -2 } },
        { { 0, -1, 0 }, { -1, -1, -1 }, { 2, 0, 0 }, { 0, 0, 2 } },
        { { 0, 0, 1 }, { -1, -1, 1 }, { 2, 0, 0 }, { 0, 2, 0 } },
        { { 0, 0, -1 }, { 1, -1, -1 }, { -2, 0, 0 }, { 0, 2, 0 } },
    } };

    MeshData cube;
    cube.vertices.reserve(faces.size() * 4);
    cube.indices.reserve(faces.size() * 6);

    for (const auto& face: faces) {
        const auto base = static_cast<uint32_t>(cube.vertices.size());
        const std::array<glm::vec2, 4> uvs{ { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f },
            { 0.0f, 0.0f } } };
        const std::array<glm::vec3, 4> corners{ {
            face.origin,
            face.origin + face.right,
            face.origin + face.right + face.up,
            face.origin + face.up,
        } };

        for (size_t corner = 0; corner < corners.size(); ++corner) {
            rendering::MeshVertex vertex;
            vertex.position = corners[corner] * 0.5f;
            vertex.normal = face.normal;
            vertex.tangent = glm::normalize(face.right);
            vertex.bitangent = glm::normalize(face.up);
            vertex.uv = uvs[corner];
            cube.vertices.push_back(vertex);
        }

        for (const uint32_t offset: { 0U, 1U, 2U, 0U, 2U, 3U }) {
            cube.indices.push_back(base + offset);
        }
    }
    return cube;
}

// UV 球：外接于 cube 的单位直径球，按经纬网格铺开。顶点属性全部解析求出 ——
// 法线就是单位化的位置，切线沿 u（经度）方向，副切线沿 v（纬度）方向取反，
// 这样 T×B=N 的手性和 makeCube() 的每个面一致。
//
// 两极的 ∂P/∂φ 退化成零向量，所以切线不能靠差分算；解析式在极点仍然是单位长度，
// 只是它的方向取决于该列的 u —— 这正是极点要按 sector 复制一圈顶点的原因，
// 否则极点附近的 UV 和切线都会被挤成一个值。
MeshData makeSphere() {
    constexpr uint32_t kSectors = 32; // 经度分段（绕 Y 轴一圈）
    constexpr uint32_t kStacks = 16;  // 纬度分段（北极到南极）
    constexpr float kRadius = 0.5f;   // 和 cube 的半宽一致，默认缩放下大小可比

    MeshData sphere;
    sphere.vertices.reserve(static_cast<size_t>(kSectors + 1) * (kStacks + 1));
    sphere.indices.reserve(static_cast<size_t>(kSectors) * kStacks * 6);

    for (uint32_t stack = 0; stack <= kStacks; ++stack) {
        const float v = static_cast<float>(stack) / static_cast<float>(kStacks);
        const float theta = v * glm::pi<float>();
        const float sin_theta = std::sin(theta);
        const float cos_theta = std::cos(theta);

        for (uint32_t sector = 0; sector <= kSectors; ++sector) {
            const float u = static_cast<float>(sector) / static_cast<float>(kSectors);
            const float phi = u * glm::two_pi<float>();
            const float sin_phi = std::sin(phi);
            const float cos_phi = std::cos(phi);

            rendering::MeshVertex vertex;
            vertex.normal = { sin_theta * sin_phi, cos_theta, sin_theta * cos_phi };
            vertex.position = vertex.normal * kRadius;
            vertex.tangent = { cos_phi, 0.0f, -sin_phi };
            vertex.bitangent = { -cos_theta * sin_phi, sin_theta, -cos_theta * cos_phi };
            vertex.uv = { u, v };
            sphere.vertices.push_back(vertex);
        }
    }

    const uint32_t row = kSectors + 1;
    for (uint32_t stack = 0; stack < kStacks; ++stack) {
        for (uint32_t sector = 0; sector < kSectors; ++sector) {
            const uint32_t top_left = stack * row + sector;
            const uint32_t top_right = top_left + 1;
            const uint32_t bottom_left = top_left + row;
            const uint32_t bottom_right = bottom_left + 1;

            // 极点那一圈的四边形只剩一个三角形 —— 另一个的两个顶点重合，退化成零面积。
            if (stack != 0) {
                for (const uint32_t index: { top_left, bottom_left, top_right }) {
                    sphere.indices.push_back(index);
                }
            }
            if (stack + 1 != kStacks) {
                for (const uint32_t index: { bottom_left, bottom_right, top_right }) {
                    sphere.indices.push_back(index);
                }
            }
        }
    }
    return sphere;
}

std::vector<std::byte> encodeSingleSubmesh(const MeshData& mesh) {
    rendering::Submesh submesh;
    submesh.index_offset = 0;
    submesh.index_count = static_cast<uint32_t>(mesh.indices.size());
    submesh.vertex_offset = 0;
    submesh.vertex_count = static_cast<uint32_t>(mesh.vertices.size());
    submesh.material_index = 0;

    return detail::encodeMeshArtifact(mesh.vertices, mesh.indices, { submesh }, { "Default" });
}

std::vector<std::byte> encodeCube() {
    return encodeSingleSubmesh(makeCube());
}

std::vector<std::byte> encodeSphere() {
    return encodeSingleSubmesh(makeSphere());
}

std::vector<std::byte> encodeDefaultMaterial() {
    MaterialAsset::Params params;
    params.type = rendering::MaterialType::PBR;
    params.roughness_strength = 0.5f;
    params.metallic_strength = 1.0f;
    return encodeMaterialArtifact(params);
}

struct BuiltinDescriptor {
    core::UUID handle;
    std::string_view type;
    // 展示用的虚拟身份，Assets/ 下并不存在这个文件，也不会为它写 .meta。
    std::filesystem::path source_path;
    std::filesystem::path artifact_path;
    std::vector<std::byte> (*encode)();
};

std::vector<BuiltinDescriptor> builtinDescriptors() {
    return {
        { kBuiltinCubeMesh, kMeshAssetType, "Builtin/Cube.mesh",
            std::filesystem::path{ "Builtin" } / "Cube.mesh", &encodeCube },
        { kBuiltinSphereMesh, kMeshAssetType, "Builtin/Sphere.mesh",
            std::filesystem::path{ "Builtin" } / "Sphere.mesh", &encodeSphere },
        { kBuiltinDefaultMaterial, kMaterialAssetType, "Builtin/Default.material",
            std::filesystem::path{ "Builtin" } / "Default.material", &encodeDefaultMaterial },
    };
}

}

// builtin 资产的身份全部是编译期常量，所以磁盘上不需要 .meta —— 写了反而让
// 派生数据有权覆盖代码里的定义，还会污染用户的 Assets/ 与版本库。
// 这里每次都按 artifact 是否真的存在来决定是否重新生成，从而保证
// Library/ 被整个删掉后仍然能自愈。
bool ensureBuiltinAssets(arti::asset::AssetManager& assets) {
    // 同时注册成 provider，让后续每轮 reconcile 走同一条自愈路径。
    assets.registerEngineAssetProvider(
            [](arti::asset::AssetManager& manager) { return restoreBuiltinAssets(manager); });
    return restoreBuiltinAssets(assets);
}

bool restoreBuiltinAssets(arti::asset::AssetManager& assets) {
    bool ok = true;
    for (const BuiltinDescriptor& builtin: builtinDescriptors()) {
        if (!assets.storage().hasArtifact(builtin.artifact_path)) {
            if (!assets.storage().writeArtifact(builtin.artifact_path, builtin.encode())) {
                ok = false;
                continue;
            }
        }

        arti::asset::AssetMetadata metadata;
        metadata.handle = builtin.handle;
        metadata.type = std::string{ builtin.type };
        // local_id 空：builtin 的虚拟源路径本身就唯一标识它。
        metadata.source_path = builtin.source_path;
        metadata.artifact_path = builtin.artifact_path;

        if (assets.catalog().insert(std::move(metadata), arti::asset::AssetOrigin::Engine) ==
                arti::asset::AssetInsertStatus::Conflicted) {
            ok = false;
        }
    }
    return ok;
}

}
