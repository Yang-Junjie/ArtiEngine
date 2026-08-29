#include "asset/builtin_assets.h"

#include "artichoco/asset/asset_manager.h"
#include "asset/detail/mesh_artifact.h"
#include "asset/loaders/material_loader.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"

#include <array>
#include <glm/geometric.hpp>

namespace arti::engine::asset {
namespace {

// 立方体：每个面 4 个顶点，方便给独立的法线和 UV。
// 三角形按「从外面看逆时针」编写，与 ArtiRenderer 的 opaque pass 的正面约定一致。
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

// 写一份内置资产：artifact + .meta + 进 catalog。
bool writeBuiltin(arti::asset::AssetManager& assets, core::UUID handle, std::string_view type,
        const std::filesystem::path& source_path, const std::filesystem::path& artifact_path,
        const std::vector<std::byte>& artifact) {
    if (!assets.storage().writeArtifact(artifact_path, artifact)) {
        return false;
    }

    arti::asset::AssetMetadata metadata;
    metadata.handle = handle;
    metadata.type = std::string{ type };
    // source_path 是合成的，磁盘上没有这个文件。isSafeAssetRelativePath 只校验路径形状
    // （不是绝对路径、不含 ..），不要求存在，所以这样是合法的。
    metadata.source_path = source_path;
    metadata.artifact_path = artifact_path;

    if (!assets.storage().writeMetadata(metadata)) {
        return false;
    }
    assets.catalog().insert(std::move(metadata));
    return true;
}

} // namespace

bool ensureBuiltinAssets(arti::asset::AssetManager& assets) {
    // 幂等：catalog 里已经有就不重写。打开项目时 scanMetadata 会把上次写的读回来。
    const bool cube_present = assets.catalog().find(kBuiltinCubeMesh).has_value();
    const bool material_present = assets.catalog().find(kBuiltinDefaultMaterial).has_value();
    const bool pbr_present = assets.catalog().find(kBuiltinPbrMaterial).has_value();
    if (cube_present && material_present && pbr_present) {
        return true;
    }

    bool ok = true;

    if (!cube_present) {
        const auto cube = makeCube();
        // 一个 submesh 覆盖整个网格，槽名 "Default"。
        std::vector<rendering::Submesh> submeshes;
        rendering::Submesh submesh;
        submesh.index_offset = 0;
        submesh.index_count = static_cast<uint32_t>(cube.indices.size());
        submesh.vertex_offset = 0;
        submesh.vertex_count = static_cast<uint32_t>(cube.vertices.size());
        submesh.material_index = 0;
        submeshes.push_back(submesh);

        const auto artifact =
                detail::encodeMeshArtifact(cube.vertices, cube.indices, submeshes, { "Default" });
        ok = writeBuiltin(assets, kBuiltinCubeMesh, kMeshAssetType, "Builtin/Cube.mesh",
                     std::filesystem::path{ "Builtin" } / "Cube.mesh", artifact) &&
             ok;
    }

    if (!material_present) {
        const auto artifact = encodeMaterialArtifact(MaterialAsset::Params{});
        ok = writeBuiltin(assets, kBuiltinDefaultMaterial, kMaterialAssetType,
                     "Builtin/Default.material",
                     std::filesystem::path{ "Builtin" } / "Default.material", artifact) &&
             ok;
    }

    if (!pbr_present) {
        MaterialAsset::Params params;
        params.type = rendering::MaterialType::PBR;
        // 一块中等粗糙的白色介质。metallic 留 0：金属在没有 IBL 的时候只有环境项那一点点
        // 镜面，看起来像坏的，不适合当默认值。
        params.roughness_strength = 0.5f;
        params.metallic_strength = 0.0f;
        const auto artifact = encodeMaterialArtifact(params);
        ok = writeBuiltin(assets, kBuiltinPbrMaterial, kMaterialAssetType,
                     "Builtin/DefaultPbr.material",
                     std::filesystem::path{ "Builtin" } / "DefaultPbr.material", artifact) &&
             ok;
    }

    return ok;
}

} // namespace arti::engine::asset
