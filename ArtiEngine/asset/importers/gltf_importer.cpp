#include "asset/importers/gltf_importer.h"

#include "asset/detail/mesh_artifact.h"
#include "asset/detail/prefab_artifact.h"
#include "asset/detail/texture_artifact.h"
#include "asset/importers/detail/gltf_parser.h"
#include "asset/importers/detail/local_id.h"
#include "asset/importers/texture_importer.h"
#include "asset/loaders/material_loader.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "asset/prefab_asset.h"
#include "asset/texture_asset.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace arti::engine::asset {
namespace {

using TextureAssetHandle = arti::asset::AssetHandle<TextureAsset>;

bool hasHdrExtension(const std::filesystem::path& file) {
    auto extension = file.extension().string();
    std::ranges::transform(extension, extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".hdr";
}

}

std::vector<std::string> GltfImporter::getSupportedExtensions() const {
    return { ".gltf", ".glb" };
}

std::vector<arti::asset::SettingDescriptor> GltfImporter::getSettingSchema() const {
    arti::asset::SettingDescriptor extracted;
    extracted.key = kExtractedMaterialPrefix;
    extracted.is_prefix = true;
    // 值是 `.artimaterial` 的 Assets-relative 路径。
    extracted.default_value = std::string{};
    extracted.doc = "Per-material override: the extracted .artimaterial source that "
                    "replaces this container's own derived material.";
    return { std::move(extracted) };
}

arti::asset::SourcePrescan GltfImporter::prescan(
        const std::filesystem::path& source_path) const {
    arti::asset::SourcePrescan prescan;
    try {
        // 提取出的材质必须先导入，prefab 才能引用它的 handle —— 所以要声明成
        // 引用，让拓扑排序把 `.artimaterial` 排在本文件之前。
        if (const auto sidecar = m_storage->readMetadata(source_path)) {
            for (const auto& [key, value]: sidecar->settings.authored) {
                if (!key.starts_with(kExtractedMaterialPrefix)) {
                    continue;
                }
                if (const auto* text = std::get_if<std::string>(&value)) {
                    const std::filesystem::path path{ *text };
                    if (arti::asset::isSafeAssetRelativePath(path)) {
                        prescan.referenced_sources.push_back(path);
                    }
                }
            }
        }

        const auto file = resolveSourceFile(source_path);
        for (const auto& usage: detail::prescanGltfImages(file)) {
            // 外部图片可能落在 Assets/ 外面（相对路径逃出去）；那种情况我们
            // 无法把它当资产引用，交给 import() 自己解码。
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
        // prescan 抛异常等价于"什么都没声明"：import() 会照旧自己解码贴图。
        return {};
    }
    return prescan;
}

arti::asset::AssetImportResult GltfImporter::import(
        const arti::asset::AssetImportRequest& request) {
    const std::filesystem::path& source_path = request.source_path;
    arti::asset::AssetImportResult result;
    try {
        const auto file = resolveSourceFile(source_path);
        const detail::GltfScene scene = detail::parseGltf(file);

        // local_id 用源文件里的名字，不用下标 —— 见 local_id.h。
        detail::LocalIdAllocator ids;

        std::vector<bool> is_srgb(scene.images.size(), false);
        std::vector<bool> is_linear(scene.images.size(), false);
        for (const auto& material: scene.materials) {
            const auto mark = [&](int image, std::vector<bool>& flags) {
                if (image >= 0 && static_cast<size_t>(image) < flags.size()) {
                    flags[static_cast<size_t>(image)] = true;
                }
            };
            mark(material.base_color_image, is_srgb);
            mark(material.emissive_image, is_srgb);
            mark(material.metallic_roughness_image, is_linear);
            mark(material.normal_image, is_linear);
            mark(material.occlusion_image, is_linear);
        }

        std::vector<core::UUID> image_handles(scene.images.size());
        for (size_t index = 0; index < scene.images.size(); ++index) {
            const auto& image = scene.images[index];
            const bool embedded = image.file.empty();

            // 外部图片文件本身就是一个纹理资产（由 TextureImporter 导入），
            // 这里引用它的产出而不是再解码一份 —— 一个源文件一个资产。
            // 颜色空间正确性由 prescan 的推断保证：我们已经告诉 TextureImporter
            // 这张图绑在哪个槽上。
            //
            // reconcile 的拓扑序保证贴图先于本文件导入，所以这个查询通常命中。
            // 查不到（比如用户单独调 importFile 而没走 reconcile）就退回自己解码。
            if (!embedded) {
                if (const auto relative = m_storage->relativeSourcePath(image.file)) {
                    if (const auto existing =
                                    m_catalog->findBySourceAndLocalId(*relative, std::string{})) {
                        image_handles[index] = existing->handle;
                        continue;
                    }
                }
            }

            detail::DecodedImage decoded;
            rendering::TextureFormat format{};
            const bool hdr = !embedded && hasHdrExtension(image.file);
            if (hdr) {
                decoded = detail::decodeImageRGBA16F(image.file);
                format = rendering::TextureFormat::RGBA16Float;
            } else {
                const bool srgb = is_srgb[index] && !is_linear[index];
                format = srgb ? rendering::TextureFormat::RGBA8Srgb
                              : rendering::TextureFormat::RGBA8Unorm;
                if (embedded) {
                    decoded = detail::decodeImageMemory(image.bytes);
                } else {
                    std::error_code error;
                    if (!std::filesystem::is_regular_file(image.file, error)) {
                        continue;
                    }
                    decoded = detail::decodeImageFile(image.file);
                }
            }

            auto output = startOutput(source_path, ids.allocate("texture", image.key, index),
                    kTextureAssetType, ".artitexture");
            output.encoded = detail::encodeTextureArtifact(decoded.rgba, decoded.width,
                    decoded.height, format, true);
            image_handles[index] = output.record.handle;
            result.outputs.push_back(std::move(output));
        }

        const auto imageHandle = [&image_handles](int index) {
            return index >= 0 && static_cast<size_t>(index) < image_handles.size()
                    ? image_handles[static_cast<size_t>(index)]
                    : core::UUID{};
        };

        // Extract 的覆盖表：local_id → `.artimaterial` 源路径。
        std::unordered_map<std::string, std::filesystem::path> extracted;
        if (request.settings != nullptr) {
            for (const auto& [key, value]:
                    request.settings->withPrefix(kExtractedMaterialPrefix)) {
                if (const auto* text = std::get_if<std::string>(&value)) {
                    if (text->empty()) {
                        continue;
                    }
                    extracted.emplace(key.substr(std::strlen(kExtractedMaterialPrefix)),
                            std::filesystem::path{ *text });
                }
            }
        }

        std::vector<core::UUID> material_handles;
        material_handles.reserve(scene.materials.size());
        for (size_t index = 0; index < scene.materials.size(); ++index) {
            const auto& material = scene.materials[index];
            const std::string local_id = ids.allocate("material", material.name, index);

            // 这个槽位被提取过：引用提取物的 handle，不再产出自己那份派生材质。
            // 拓扑序保证 `.artimaterial` 已经导入；查不到就退回自己产出。
            if (const auto override_it = extracted.find(local_id);
                    override_it != extracted.end()) {
                if (const auto existing = m_catalog->findBySourceAndLocalId(override_it->second,
                            std::string{})) {
                    material_handles.push_back(existing->handle);
                    continue;
                }
            }

            auto output = startOutput(source_path, local_id, kMaterialAssetType, ".artimaterial");

            MaterialAsset::Params params;
            params.type = rendering::MaterialType::PBR;
            params.base_color = material.base_color;
            params.metallic_strength = material.metallic;
            params.roughness_strength = material.roughness;
            params.occlusion_strength = material.occlusion;
            params.emissive_strength = material.emissive;

            const core::UUID base_color = imageHandle(material.base_color_image);
            const core::UUID metallic_roughness = imageHandle(material.metallic_roughness_image);
            const core::UUID normal = imageHandle(material.normal_image);
            const core::UUID occlusion = imageHandle(material.occlusion_image);
            const core::UUID emissive = imageHandle(material.emissive_image);

            params.base_color_texture = TextureAssetHandle{ base_color };
            params.metallic_roughness_texture = TextureAssetHandle{ metallic_roughness };
            params.normal_texture = TextureAssetHandle{ normal };
            params.occlusion_texture = TextureAssetHandle{ occlusion };
            params.emissive_texture = TextureAssetHandle{ emissive };

            for (const core::UUID texture:
                    { base_color, metallic_roughness, normal, occlusion, emissive }) {
                if (texture.isValid()) {
                    output.record.dependencies.push_back(texture);
                }
            }
            output.encoded = encodeMaterialArtifact(params);

            material_handles.push_back(output.record.handle);
            result.outputs.push_back(std::move(output));
        }

        std::vector<core::UUID> mesh_handles(scene.meshes.size());
        for (size_t index = 0; index < scene.meshes.size(); ++index) {
            const auto& mesh = scene.meshes[index];
            if (mesh.vertices.empty() || mesh.indices.empty()) {
                continue;
            }

            auto output = startOutput(source_path, ids.allocate("mesh", mesh.name, index),
                    kMeshAssetType, ".artimesh");
            output.encoded = detail::encodeMeshArtifact(mesh.vertices, mesh.indices,
                    mesh.submeshes, mesh.material_slots);
            mesh_handles[index] = output.record.handle;
            result.outputs.push_back(std::move(output));
        }

        auto prefab_output = startOutput(source_path, "prefab", kPrefabAssetType,
                ".artiprefab");
        std::vector<PrefabNode> nodes;
        nodes.reserve(scene.nodes.size() + 1);

        PrefabNode root;
        root.name = source_path.stem().string();
        nodes.push_back(std::move(root));

        for (const auto& source_node: scene.nodes) {
            PrefabNode node;
            node.name = source_node.name;
            node.local_transform = source_node.local_transform;
            node.parent = source_node.parent == kNoParentNode ? 0 : source_node.parent + 1;

            if (source_node.mesh >= 0 &&
                    static_cast<size_t>(source_node.mesh) < mesh_handles.size()) {
                const auto mesh_index = static_cast<size_t>(source_node.mesh);
                node.mesh = mesh_handles[mesh_index];
                const auto& mesh = scene.meshes[mesh_index];
                node.materials.reserve(mesh.material_indices.size());
                for (const int material_index: mesh.material_indices) {
                    node.materials.push_back(material_index >= 0 &&
                                            static_cast<size_t>(material_index) <
                                                    material_handles.size()
                                    ? material_handles[static_cast<size_t>(material_index)]
                                    : core::UUID{});
                }
            }
            nodes.push_back(std::move(node));
        }
        prefab_output.encoded = detail::encodePrefabArtifact(nodes);

        for (const core::UUID mesh: mesh_handles) {
            if (mesh.isValid()) {
                prefab_output.record.dependencies.push_back(mesh);
            }
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
