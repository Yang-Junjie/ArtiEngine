#include "asset/importers/obj_importer.h"

#include "asset/importers/detail/local_id.h"
#include "asset/importers/detail/mtl_scan.h"
#include "asset/importers/texture_importer.h"

#include "asset/detail/mesh_artifact.h"
#include "asset/detail/prefab_artifact.h"
#include "asset/detail/texture_artifact.h"
#include "asset/loaders/material_loader.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "asset/prefab_asset.h"
#include "asset/texture_asset.h"

#include <algorithm>
#include <glm/geometric.hpp>
#include <stdexcept>
#include <tiny_obj_loader.h>
#include <unordered_map>

namespace arti::engine::asset {
namespace {

using TextureAssetHandle = arti::asset::AssetHandle<TextureAsset>;

struct SubmeshRun {
    uint32_t index_offset{ 0 };
    uint32_t index_count{ 0 };
    int material_id{ -1 };
};

struct ParsedMesh {
    std::string name;
    std::vector<rendering::MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubmeshRun> runs;
};

bool hasPbrFields(const tinyobj::material_t& material) {
    return material.roughness > 0.0f || material.metallic > 0.0f ||
           !material.roughness_texname.empty() || !material.metallic_texname.empty();
}

MaterialAsset::Params paramsFromMtl(const tinyobj::material_t& material) {
    MaterialAsset::Params params;
    params.type = hasPbrFields(material) ? rendering::MaterialType::PBR
                                        : rendering::MaterialType::BlinnPhong;
    params.base_color = { material.diffuse[0], material.diffuse[1], material.diffuse[2],
        material.dissolve };
    // TODO(patch): MTL 的 map_Kd 语义是替换 Kd，而着色端做的是 base_color * texture。
    // Maya / 3ds Max 在贴图接管漫反射时写 Kd 0 0 0，照抄会让模型全黑，所以这里抬回白色。
    if (!material.diffuse_texname.empty() && params.base_color.r == 0.0f &&
            params.base_color.g == 0.0f && params.base_color.b == 0.0f) {
        params.base_color = { 1.0f, 1.0f, 1.0f, params.base_color.a };
    }
    params.specular_color = { material.specular[0], material.specular[1], material.specular[2] };
    // TODO(patch): 阈值是 1 而不是 0，因为 tinyobj 的 InitMaterial 在 MTL 没写 Ns 时把
    // shininess 填成 1，按 > 0 判断 fallback 永远进不去。代价是真写了 Ns 1 的材质被当成没写。
    params.shininess = material.shininess > 1.0f ? material.shininess : 32.0f;
    params.specular_strength = glm::length(
            glm::vec3{ material.specular[0], material.specular[1], material.specular[2] });
    params.metallic_strength = material.metallic;
    params.roughness_strength = material.roughness > 0.0f ? material.roughness : 1.0f;
    return params;
}

struct VertexKey {
    int position{ -1 };
    int normal{ -1 };
    int uv{ -1 };

    bool operator==(const VertexKey&) const = default;
};

struct VertexKeyHash {
    size_t operator()(const VertexKey& key) const noexcept {
        const uint64_t packed = (static_cast<uint64_t>(key.position + 1) << 42) ^
                                (static_cast<uint64_t>(key.normal + 1) << 21) ^
                                static_cast<uint64_t>(key.uv + 1);
        return std::hash<uint64_t>{}(packed);
    }
};

glm::vec3 faceNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 edge0 = b - a;
    const glm::vec3 edge1 = c - a;
    const glm::vec3 normal = glm::cross(edge0, edge1);
    const float length = glm::length(normal);
    return length > 0.0f ? normal / length : glm::vec3{ 0.0f, 1.0f, 0.0f };
}

void makeTangentBasis(const glm::vec3& normal, glm::vec3& tangent, glm::vec3& bitangent) {
    const glm::vec3 reference = std::abs(normal.y) < 0.99f ? glm::vec3{ 0.0f, 1.0f, 0.0f }
                                                           : glm::vec3{ 1.0f, 0.0f, 0.0f };
    tangent = glm::normalize(glm::cross(reference, normal));
    bitangent = glm::cross(normal, tangent);
}

ParsedMesh buildMesh(const tinyobj::attrib_t& attrib, const tinyobj::shape_t& shape) {
    ParsedMesh mesh;
    mesh.name = shape.name.empty() ? "Mesh" : shape.name;

    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> lookup;
    const auto& mesh_indices = shape.mesh.indices;
    const auto& material_ids = shape.mesh.material_ids;

    int current_material = -2;
    for (size_t face = 0; face * 3 < mesh_indices.size(); ++face) {
        const int material_id =
                face < material_ids.size() ? material_ids[face] : -1;
        if (material_id != current_material) {
            SubmeshRun run;
            run.index_offset = static_cast<uint32_t>(mesh.indices.size());
            run.material_id = material_id;
            mesh.runs.push_back(run);
            current_material = material_id;
        }

        std::array<glm::vec3, 3> positions{};
        std::array<VertexKey, 3> keys{};
        for (int corner = 0; corner < 3; ++corner) {
            const tinyobj::index_t& index = mesh_indices[face * 3 + corner];
            keys[corner] = VertexKey{ index.vertex_index, index.normal_index,
                index.texcoord_index };
            positions[corner] = { attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2] };
        }

        const bool has_normals = keys[0].normal >= 0;
        const glm::vec3 computed_normal =
                has_normals ? glm::vec3{ 0.0f }
                            : faceNormal(positions[0], positions[1], positions[2]);

        for (int corner = 0; corner < 3; ++corner) {
            const VertexKey& key = keys[corner];
            uint32_t vertex_index = 0;

            const auto found = has_normals ? lookup.find(key) : lookup.end();
            if (found != lookup.end()) {
                vertex_index = found->second;
            } else {
                rendering::MeshVertex vertex;
                vertex.position = positions[corner];
                vertex.normal = has_normals
                        ? glm::vec3{ attrib.normals[3 * key.normal + 0],
                              attrib.normals[3 * key.normal + 1],
                              attrib.normals[3 * key.normal + 2] }
                        : computed_normal;
                if (key.uv >= 0) {
                    vertex.uv = { attrib.texcoords[2 * key.uv + 0],
                        1.0f - attrib.texcoords[2 * key.uv + 1] };
                }
                makeTangentBasis(vertex.normal, vertex.tangent, vertex.bitangent);

                vertex_index = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(vertex);
                if (has_normals) {
                    lookup.emplace(key, vertex_index);
                }
            }
            mesh.indices.push_back(vertex_index);
        }
    }

    for (size_t run = 0; run < mesh.runs.size(); ++run) {
        const uint32_t next_offset = run + 1 < mesh.runs.size()
                ? mesh.runs[run + 1].index_offset
                : static_cast<uint32_t>(mesh.indices.size());
        mesh.runs[run].index_count = next_offset - mesh.runs[run].index_offset;
    }
    return mesh;
}

}

std::vector<std::string> ObjImporter::getSupportedExtensions() const {
    return { ".obj" };
}

arti::asset::SourcePrescan ObjImporter::prescan(
        const std::filesystem::path& source_path) const {
    arti::asset::SourcePrescan prescan;
    try {
        const auto file = resolveSourceFile(source_path);
        for (const auto& usage: detail::scanMtlTextures(file)) {
            // 贴图可能落在 Assets/ 外面；那种情况无法当资产引用，
            // 交给 import() 自己解码。
            const auto relative = m_storage->relativeSourcePath(usage.file);
            if (!relative) {
                continue;
            }
            prescan.referenced_sources.push_back(*relative);
            prescan.suggestions.push_back({ *relative,
                std::string{ TextureImporter::kColorspaceSetting },
                std::string{ usage.is_color ? TextureImporter::kColorspaceSrgb
                                            : TextureImporter::kColorspaceLinear },
                usage.usage });
        }
    } catch (...) {
        return {};
    }
    return prescan;
}

arti::asset::AssetImportResult ObjImporter::import(
        const arti::asset::AssetImportRequest& request) {
    const std::filesystem::path& source_path = request.source_path;
    arti::asset::AssetImportResult result;
    try {
        const auto file = resolveSourceFile(source_path);

        tinyobj::ObjReaderConfig config;
        config.triangulate = true;
        config.mtl_search_path = file.parent_path().string();

        tinyobj::ObjReader reader;
        if (!reader.ParseFromFile(file.string(), config)) {
            result.error = reader.Error().empty() ? "Failed to parse the OBJ file."
                                                  : reader.Error();
            return result;
        }

        const auto& attrib = reader.GetAttrib();
        const auto& shapes = reader.GetShapes();
        const auto& materials = reader.GetMaterials();
        if (shapes.empty()) {
            result.error = "The OBJ file contains no shapes.";
            return result;
        }

        // local_id 用源文件里的名字，不用下标 —— 见 local_id.h。
        detail::LocalIdAllocator ids;

        std::unordered_map<std::string, core::UUID> texture_handles;
        const auto importTexture = [&](const std::string& texname,
                                            rendering::TextureFormat format) -> core::UUID {
            if (texname.empty()) {
                return {};
            }
            const auto existing = texture_handles.find(texname);
            if (existing != texture_handles.end()) {
                return existing->second;
            }

            const auto texture_file = (file.parent_path() / texname).lexically_normal();
            std::error_code error;
            if (!std::filesystem::is_regular_file(texture_file, error)) {
                return {};
            }

            // 贴图文件本身就是一个纹理资产（由 TextureImporter 导入），
            // 这里引用它的产出而不是再解码一份。颜色空间正确性由 prescan
            // 的推断保证。拓扑序让贴图先导入，所以这个查询通常命中；
            // 查不到就退回自己解码。
            if (const auto relative = m_storage->relativeSourcePath(texture_file)) {
                if (const auto shared =
                                m_catalog->findBySourceAndLocalId(*relative, std::string{})) {
                    texture_handles.emplace(texname, shared->handle);
                    return shared->handle;
                }
            }

            auto output = startOutput(source_path, ids.allocate("texture", texname, 0),
                    kTextureAssetType, ".artitexture");
            const auto image = detail::decodeImageFile(texture_file);
            output.encoded = detail::encodeTextureArtifact(image.rgba, image.width, image.height,
                    format, true);

            const core::UUID handle = output.record.handle;
            texture_handles.emplace(texname, handle);
            result.outputs.push_back(std::move(output));
            return handle;
        };

        std::vector<core::UUID> material_handles;
        material_handles.reserve(materials.size());
        for (size_t index = 0; index < materials.size(); ++index) {
            const auto& material = materials[index];
            auto output = startOutput(source_path, ids.allocate("material", material.name, index),
                    kMaterialAssetType, ".artimaterial");

            auto params = paramsFromMtl(material);
            const core::UUID base_color = importTexture(material.diffuse_texname,
                    rendering::TextureFormat::RGBA8Srgb);
            const core::UUID normal = importTexture(
                    material.normal_texname.empty() ? material.bump_texname
                                                    : material.normal_texname,
                    rendering::TextureFormat::RGBA8Unorm);
            const core::UUID roughness = importTexture(material.roughness_texname,
                    rendering::TextureFormat::RGBA8Unorm);
            const core::UUID metallic = importTexture(material.metallic_texname,
                    rendering::TextureFormat::RGBA8Unorm);
            const core::UUID occlusion = importTexture(material.ambient_texname,
                    rendering::TextureFormat::RGBA8Unorm);
            const core::UUID emissive = importTexture(material.emissive_texname,
                    rendering::TextureFormat::RGBA8Srgb);

            params.base_color_texture = TextureAssetHandle{ base_color };
            params.normal_texture = TextureAssetHandle{ normal };
            // TODO(patch): MTL 把 roughness 和 metallic 分成两张图，渲染端只有一个合并槽
            // （G=roughness、B=metallic 的 glTF 约定），所以只能二选一。两张都有时会丢一张。
            params.metallic_roughness_texture =
                    TextureAssetHandle{ roughness.isValid() ? roughness : metallic };
            params.occlusion_texture = TextureAssetHandle{ occlusion };
            params.emissive_texture = TextureAssetHandle{ emissive };

            for (const core::UUID texture:
                    { base_color, normal, roughness, metallic, occlusion, emissive }) {
                if (texture.isValid()) {
                    output.record.dependencies.push_back(texture);
                }
            }
            output.encoded = encodeMaterialArtifact(params);

            material_handles.push_back(output.record.handle);
            result.outputs.push_back(std::move(output));
        }

        std::vector<core::UUID> mesh_handles;
        std::vector<std::vector<core::UUID>> mesh_materials;
        mesh_handles.reserve(shapes.size());
        for (size_t index = 0; index < shapes.size(); ++index) {
            const ParsedMesh parsed = buildMesh(attrib, shapes[index]);
            if (parsed.vertices.empty() || parsed.indices.empty()) {
                continue;
            }

            auto output = startOutput(source_path, ids.allocate("mesh", parsed.name, index),
                    kMeshAssetType, ".artimesh");

            std::vector<rendering::Submesh> submeshes;
            std::vector<std::string> slots;
            std::vector<core::UUID> slot_materials;
            submeshes.reserve(parsed.runs.size());
            for (const SubmeshRun& run: parsed.runs) {
                rendering::Submesh submesh;
                submesh.index_offset = run.index_offset;
                submesh.index_count = run.index_count;
                submesh.vertex_offset = 0;
                submesh.vertex_count = static_cast<uint32_t>(parsed.vertices.size());
                submesh.material_index = static_cast<uint32_t>(submeshes.size());
                submeshes.push_back(submesh);

                const bool valid_material = run.material_id >= 0 &&
                        static_cast<size_t>(run.material_id) < materials.size();
                slots.push_back(valid_material ? materials[run.material_id].name : "Default");
                slot_materials.push_back(
                        valid_material ? material_handles[run.material_id] : core::UUID{});
            }

            output.encoded = detail::encodeMeshArtifact(parsed.vertices, parsed.indices, submeshes,
                    slots);
            mesh_handles.push_back(output.record.handle);
            mesh_materials.push_back(std::move(slot_materials));
            result.outputs.push_back(std::move(output));
        }

        if (mesh_handles.empty()) {
            result.error = "The OBJ file produced no usable meshes.";
            return result;
        }

        auto prefab_output = startOutput(source_path, "prefab", kPrefabAssetType,
                ".artiprefab");
        std::vector<PrefabNode> nodes;
        nodes.reserve(mesh_handles.size() + 1);

        PrefabNode root;
        root.name = source_path.stem().string();
        nodes.push_back(std::move(root));

        for (size_t index = 0; index < mesh_handles.size(); ++index) {
            PrefabNode node;
            node.name = "Mesh " + std::to_string(index);
            node.parent = 0;
            node.mesh = mesh_handles[index];
            node.materials = mesh_materials[index];
            nodes.push_back(std::move(node));
        }
        prefab_output.encoded = detail::encodePrefabArtifact(nodes);

        for (const core::UUID mesh: mesh_handles) {
            prefab_output.record.dependencies.push_back(mesh);
        }
        for (const core::UUID material: material_handles) {
            prefab_output.record.dependencies.push_back(material);
        }
        result.outputs.push_back(std::move(prefab_output));
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.outputs.clear();
    }
    return result;
}

}
