#include "asset/importers/material_importer.h"

#include "asset/importers/texture_importer.h"
#include "asset/loaders/material_loader.h"
#include "asset/material_asset.h"

#include <array>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>
#include <yaml-cpp/yaml.h>

namespace arti::engine::asset {
namespace {

using TextureAssetHandle = arti::asset::AssetHandle<TextureAsset>;

struct SlotSpec {
    std::string_view key;
    std::string_view usage;
    bool is_color;
};

// 与 glTF / OBJ importer 的槽位语义一致。
constexpr std::array<SlotSpec, 5> kSlots{ {
        { "BaseColorTexture", "base_color", true },
        { "EmissiveTexture", "emissive", true },
        { "NormalTexture", "normal", false },
        { "MetallicRoughnessTexture", "metallic_roughness", false },
        { "OcclusionTexture", "occlusion", false },
} };

// 一条贴图引用：`path` 或 `path#local_id`。
struct TextureReference {
    std::filesystem::path source;
    std::string local_id;
};

std::optional<TextureReference> parseReference(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    const size_t hash = text.find('#');
    TextureReference reference;
    if (hash == std::string_view::npos) {
        reference.source = std::filesystem::path{ std::string{ text } };
    } else {
        reference.source = std::filesystem::path{ std::string{ text.substr(0, hash) } };
        reference.local_id = std::string{ text.substr(hash + 1) };
    }
    if (!arti::asset::isSafeAssetRelativePath(reference.source)) {
        return std::nullopt;
    }
    return reference;
}

std::string readSourceText(const std::filesystem::path& file) {
    std::ifstream input{ file, std::ios::binary };
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open '" + file.string() + "'.");
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

MaterialAsset::Params paramsFromNode(const YAML::Node& node) {
    MaterialAsset::Params params;
    params.type = rendering::MaterialType::PBR;
    if (const auto value = node["BaseColor"]; value && value.size() == 4) {
        params.base_color = { value[0].as<float>(), value[1].as<float>(), value[2].as<float>(),
            value[3].as<float>() };
    }
    if (const auto value = node["Metallic"]) {
        params.metallic_strength = value.as<float>();
    }
    if (const auto value = node["Roughness"]) {
        params.roughness_strength = value.as<float>();
    }
    if (const auto value = node["Occlusion"]) {
        params.occlusion_strength = value.as<float>();
    }
    if (const auto value = node["Emissive"]) {
        params.emissive_strength = value.as<float>();
    }
    return params;
}

} // namespace

std::vector<std::string> MaterialImporter::getSupportedExtensions() const {
    return { ".artimaterial" };
}

arti::asset::SourcePrescan MaterialImporter::prescan(
        const std::filesystem::path& source_path) const {
    arti::asset::SourcePrescan prescan;
    try {
        const YAML::Node node = YAML::Load(readSourceText(resolveSourceFile(source_path)));
        for (const SlotSpec& slot: kSlots) {
            const auto value = node[std::string{ slot.key }];
            if (!value || !value.IsScalar()) {
                continue;
            }
            const auto reference = parseReference(value.as<std::string>());
            if (!reference) {
                continue;
            }
            prescan.referenced_sources.push_back(reference->source);
            // 只对独立纹理发布推断：容器子资产的设置归容器语义，不该被材质改。
            if (reference->local_id.empty()) {
                prescan.suggestions.push_back({ reference->source,
                    std::string{ TextureImporter::kColorspaceSetting },
                    std::string{ slot.is_color ? TextureImporter::kColorspaceSrgb
                                               : TextureImporter::kColorspaceLinear },
                    std::string{ slot.usage } });
            }
        }
    } catch (...) {
        return {};
    }
    return prescan;
}

arti::asset::AssetImportResult MaterialImporter::import(
        const arti::asset::AssetImportRequest& request) {
    arti::asset::AssetImportResult result;
    try {
        const auto file = resolveSourceFile(request.source_path);
        const YAML::Node node = YAML::Load(readSourceText(file));

        // local_id 为空：一个 .artimaterial 就是一个材质资产。
        auto output = startOutput(request.source_path, {}, kMaterialAssetType, ".artimaterial");
        MaterialAsset::Params params = paramsFromNode(node);

        // 路径引用 → UUID。拓扑序保证被引用的纹理已经导入。
        const auto resolve = [&](std::string_view key) -> core::UUID {
            const auto value = node[std::string{ key }];
            if (!value || !value.IsScalar()) {
                return {};
            }
            const auto reference = parseReference(value.as<std::string>());
            if (!reference) {
                return {};
            }
            const auto found =
                    m_catalog->findBySourceAndLocalId(reference->source, reference->local_id);
            return found ? found->handle : core::UUID{};
        };

        const core::UUID base_color = resolve("BaseColorTexture");
        const core::UUID metallic_roughness = resolve("MetallicRoughnessTexture");
        const core::UUID normal = resolve("NormalTexture");
        const core::UUID occlusion = resolve("OcclusionTexture");
        const core::UUID emissive = resolve("EmissiveTexture");

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

        output.record.properties["material_type"] = std::string{ "PBR" };
        output.encoded = encodeMaterialArtifact(params);
        result.outputs.push_back(std::move(output));
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.outputs.clear();
    }
    return result;
}

std::string writeMaterialSource(const MaterialAsset::Params& params,
        const MaterialSourceTextures& textures) {
    YAML::Node node;
    node["Type"] = "PBR";
    node["BaseColor"] = std::vector{ params.base_color.r, params.base_color.g, params.base_color.b,
        params.base_color.a };
    node["Metallic"] = params.metallic_strength;
    node["Roughness"] = params.roughness_strength;
    node["Occlusion"] = params.occlusion_strength;
    node["Emissive"] = params.emissive_strength;

    if (!textures.base_color.empty()) {
        node["BaseColorTexture"] = textures.base_color;
    }
    if (!textures.metallic_roughness.empty()) {
        node["MetallicRoughnessTexture"] = textures.metallic_roughness;
    }
    if (!textures.normal.empty()) {
        node["NormalTexture"] = textures.normal;
    }
    if (!textures.occlusion.empty()) {
        node["OcclusionTexture"] = textures.occlusion;
    }
    if (!textures.emissive.empty()) {
        node["EmissiveTexture"] = textures.emissive;
    }

    YAML::Emitter emitter;
    emitter << node;
    return std::string{ emitter.c_str() } + "\n";
}

}
