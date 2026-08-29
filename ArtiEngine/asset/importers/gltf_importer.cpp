#include "asset/importers/gltf_importer.h"

#include "asset/detail/mesh_artifact.h"
#include "asset/detail/prefab_artifact.h"
#include "asset/detail/texture_artifact.h"
#include "asset/importers/detail/gltf_parser.h"
#include "asset/loaders/material_loader.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "asset/prefab_asset.h"
#include "asset/texture_asset.h"

#include <algorithm>
#include <stdexcept>
#include <string>
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

std::vector<std::byte> GltfImporter::encode(const arti::asset::AssetMetadata&,
        const std::filesystem::path&) const {
    throw std::logic_error("GltfImporter produces artifacts directly in import().");
}

arti::asset::AssetImportResult GltfImporter::import(const std::filesystem::path& source_path) {
    arti::asset::AssetImportResult result;
    try {
        const auto file = resolveSourceFile(source_path);
        const detail::GltfScene scene = detail::parseGltf(file);

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

            detail::DecodedImage decoded;
            rendering::TextureFormat format{};
            const bool embedded = image.file.empty();
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

            auto output = startOutput(source_path, ".texture." + image.key, kTextureAssetType,
                    ".artitexture");
            output.encoded = detail::encodeTextureArtifact(decoded.rgba, decoded.width,
                    decoded.height, format, true);
            image_handles[index] = output.metadata.handle;
            result.outputs.push_back(std::move(output));
        }

        const auto imageHandle = [&image_handles](int index) {
            return index >= 0 && static_cast<size_t>(index) < image_handles.size()
                    ? image_handles[static_cast<size_t>(index)]
                    : core::UUID{};
        };

        std::vector<core::UUID> material_handles;
        material_handles.reserve(scene.materials.size());
        for (size_t index = 0; index < scene.materials.size(); ++index) {
            const auto& material = scene.materials[index];
            auto output = startOutput(source_path, ".material." + std::to_string(index),
                    kMaterialAssetType, ".artimaterial");

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
                    output.metadata.dependencies.push_back(texture);
                }
            }
            output.encoded = encodeMaterialArtifact(params);

            material_handles.push_back(output.metadata.handle);
            result.outputs.push_back(std::move(output));
        }

        std::vector<core::UUID> mesh_handles(scene.meshes.size());
        for (size_t index = 0; index < scene.meshes.size(); ++index) {
            const auto& mesh = scene.meshes[index];
            if (mesh.vertices.empty() || mesh.indices.empty()) {
                continue;
            }

            auto output = startOutput(source_path, ".mesh." + std::to_string(index),
                    kMeshAssetType, ".artimesh");
            output.encoded = detail::encodeMeshArtifact(mesh.vertices, mesh.indices,
                    mesh.submeshes, mesh.material_slots);
            mesh_handles[index] = output.metadata.handle;
            result.outputs.push_back(std::move(output));
        }

        auto prefab_output = startOutput(source_path, ".prefab", kPrefabAssetType, ".artiprefab");
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
                prefab_output.metadata.dependencies.push_back(mesh);
            }
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

}
