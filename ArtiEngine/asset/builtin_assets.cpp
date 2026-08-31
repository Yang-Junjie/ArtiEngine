#include "asset/builtin_assets.h"

#include "artichoco/asset/asset_manager.h"
#include "asset/detail/mesh_artifact.h"
#include "asset/loaders/material_loader.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <glm/geometric.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace arti::engine::asset {
namespace {

struct CubeGeometry {
    std::vector<rendering::MeshVertex> vertices;
    std::vector<uint32_t> indices;
};

CubeGeometry makeCube() {
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

    CubeGeometry cube;
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

std::vector<std::byte> encodeCube() {
    const auto cube = makeCube();
    rendering::Submesh submesh;
    submesh.index_offset = 0;
    submesh.index_count = static_cast<uint32_t>(cube.indices.size());
    submesh.vertex_offset = 0;
    submesh.vertex_count = static_cast<uint32_t>(cube.vertices.size());
    submesh.material_index = 0;

    return detail::encodeMeshArtifact(cube.vertices, cube.indices, { submesh }, { "Default" });
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
