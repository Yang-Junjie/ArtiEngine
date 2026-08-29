#include "asset/importers/obj_importer.h"

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
    // MTL 里 map_Kd 是**替换** Kd 而不是调制它，所以贴图完全接管漫反射时，导出器（Maya、
    // 3ds Max）习惯把 Kd 写成 0 0 0。我们的着色是 base_color * base_color_texture，照抄这个
    // 0 会让整个模型变纯黑，所以声明了漫反射贴图而 Kd 为黑时把系数抬回白色。
    //
    // 判断用 diffuse_texname 而不是贴图是否真的导入成功：文件缺失时材质会退回纯色，那种情况下
    // 白色比黑色更容易看出问题。
    if (!material.diffuse_texname.empty() && params.base_color.r == 0.0f &&
            params.base_color.g == 0.0f && params.base_color.b == 0.0f) {
        params.base_color = { 1.0f, 1.0f, 1.0f, params.base_color.a };
    }
    params.specular_color = { material.specular[0], material.specular[1], material.specular[2] };
    // MTL 的 Ns 范围是 0..1000，我们的 shininess 就是半程向量的指数，语义相同，直接取。
    // 阈值是 1 而不是 0：tinyobj 的 InitMaterial 在 MTL 没写 Ns 时把 shininess 填成 1，
    // 按 > 0 判断的话 fallback 永远不会生效。代价是真写了 Ns 1 的材质也会被当成没写。
    params.shininess = material.shininess > 1.0f ? material.shininess : 32.0f;
    params.specular_strength = glm::length(
            glm::vec3{ material.specular[0], material.specular[1], material.specular[2] });
    params.metallic_strength = material.metallic;
    params.roughness_strength = material.roughness > 0.0f ? material.roughness : 1.0f;
    return params;
}

// 顶点去重的 key。OBJ 的面索引是 (v, vn, vt) 三元组，同一个位置配不同法线要算不同顶点。
struct VertexKey {
    int position{ -1 };
    int normal{ -1 };
    int uv{ -1 };

    bool operator==(const VertexKey&) const = default;
};

struct VertexKeyHash {
    size_t operator()(const VertexKey& key) const noexcept {
        // 三个 int 拼成一个 64 位再散列。索引值都远小于 2^21，所以不会互相污染。
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

// 从法线现造一组正交切线。OBJ 不带切线数据，而法线贴图需要它。
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

    // 按材质分段。OBJ 的面已经按 usemtl 聚在一起（tinyobj 保持文件顺序），所以扫一遍
    // 遇到材质变化就开新段即可，不用先排序。
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

        // 三角形的三个角。tinyobj 已经做过三角化（triangulate=true）。
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

        // 没有法线数据时按面算一个。这时不能共享顶点（相邻面法线不同），
        // 所以 key 里的 normal_index 是 -1，去重只在同一个面内生效。
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
                    // OBJ 的 V 轴朝上，我们的纹理坐标原点在左上，所以翻一下。
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

} // namespace

std::vector<std::string> ObjImporter::getSupportedExtensions() const {
    return { ".obj" };
}

std::vector<std::byte> ObjImporter::encode(const arti::asset::AssetMetadata&,
        const std::filesystem::path&) const {
    // import() 直接产出每个子资产的字节，不走这条单资产的路径。
    throw std::logic_error("ObjImporter produces artifacts directly in import().");
}

arti::asset::AssetImportResult ObjImporter::import(const std::filesystem::path& source_path) {
    arti::asset::AssetImportResult result;
    try {
        const auto file = resolveSourceFile(source_path);

        tinyobj::ObjReaderConfig config;
        config.triangulate = true;
        // MTL 和 .obj 同目录，tinyobj 靠这个找 mtllib 引用的文件。
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

        // 一张贴图在 MTL 里可能被多个材质引用，所以按相对路径去重，一个文件只导一次。
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

            const auto texture_file = file.parent_path() / texname;
            std::error_code error;
            if (!std::filesystem::is_regular_file(texture_file, error)) {
                // 贴图缺失不让整个导入失败：材质会退回到纯色，模型仍然能用。
                return {};
            }

            auto output = startOutput(source_path, ".texture." + texname, kTextureAssetType,
                    ".artitexture");
            const auto image = detail::decodeImageFile(texture_file);
            output.encoded = detail::encodeTextureArtifact(image.rgba, image.width, image.height,
                    format, true);

            const core::UUID handle = output.metadata.handle;
            texture_handles.emplace(texname, handle);
            result.outputs.push_back(std::move(output));
            return handle;
        };

        // 材质。base_color 贴图是 sRGB，其余是线性 —— 这个区分只有在知道贴图用途时才做得对，
        // 而 MTL 的字段名正好说明了用途。
        std::vector<core::UUID> material_handles;
        material_handles.reserve(materials.size());
        for (size_t index = 0; index < materials.size(); ++index) {
            const auto& material = materials[index];
            auto output = startOutput(source_path, ".material." + std::to_string(index),
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
            // MTL 的 map_Ka 是环境光贴图，tinyobj 说它同时也当 AO 用，填进 occlusion 槽。
            const core::UUID occlusion = importTexture(material.ambient_texname,
                    rendering::TextureFormat::RGBA8Unorm);
            const core::UUID emissive = importTexture(material.emissive_texname,
                    rendering::TextureFormat::RGBA8Srgb);

            // 槽位和 rendering::Material 一一对应。MTL 把 roughness 和 metallic 分开存，
            // 渲染端只有一个合并槽，优先放 roughness，没有时退回 metallic。
            params.base_color_texture = TextureAssetHandle{ base_color };
            params.normal_texture = TextureAssetHandle{ normal };
            params.metallic_roughness_texture =
                    TextureAssetHandle{ roughness.isValid() ? roughness : metallic };
            params.occlusion_texture = TextureAssetHandle{ occlusion };
            params.emissive_texture = TextureAssetHandle{ emissive };

            // 贴图作为依赖写进 .meta，AssetManager::load 会先把它们加载好再解码材质。
            for (const core::UUID texture:
                    { base_color, normal, roughness, metallic, occlusion, emissive }) {
                if (texture.isValid()) {
                    output.metadata.dependencies.push_back(texture);
                }
            }
            output.encoded = encodeMaterialArtifact(params);

            material_handles.push_back(output.metadata.handle);
            result.outputs.push_back(std::move(output));
        }

        // 网格。一个 shape 一个 MeshAsset，submesh 按材质分段。
        std::vector<core::UUID> mesh_handles;
        std::vector<std::vector<core::UUID>> mesh_materials;
        mesh_handles.reserve(shapes.size());
        for (size_t index = 0; index < shapes.size(); ++index) {
            const ParsedMesh parsed = buildMesh(attrib, shapes[index]);
            if (parsed.vertices.empty() || parsed.indices.empty()) {
                continue;
            }

            auto output = startOutput(source_path, ".mesh." + std::to_string(index),
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
            mesh_handles.push_back(output.metadata.handle);
            mesh_materials.push_back(std::move(slot_materials));
            result.outputs.push_back(std::move(output));
        }

        if (mesh_handles.empty()) {
            result.error = "The OBJ file produced no usable meshes.";
            return result;
        }

        // Prefab：把这些网格摆成一棵树，导入后能一次拖进场景。
        // OBJ 没有节点层级，所以是一层根节点加上每个 shape 一个子节点。
        auto prefab_output = startOutput(source_path, ".prefab", kPrefabAssetType, ".artiprefab");
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
            prefab_output.metadata.dependencies.push_back(mesh);
        }
        for (const core::UUID material: material_handles) {
            prefab_output.metadata.dependencies.push_back(material);
        }
        result.outputs.push_back(std::move(prefab_output));
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.outputs.clear();
    }
    return result;
}

} // namespace arti::engine::asset
