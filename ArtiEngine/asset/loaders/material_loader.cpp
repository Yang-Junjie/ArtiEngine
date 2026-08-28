#include "asset/loaders/material_loader.h"

#include "asset/detail/artifact_io.h"

#include <cstring>
#include <yaml-cpp/yaml.h>

namespace arti::engine::asset {
namespace {

constexpr const char* materialTypeName(rendering::MaterialType type) {
    switch (type) {
        case rendering::MaterialType::Unlit:
            return "Unlit";
        case rendering::MaterialType::BlinnPhong:
            return "BlinnPhong";
        case rendering::MaterialType::PBR:
            return "PBR";
        case rendering::MaterialType::UserType:
            return "UserType";
    }
    return "BlinnPhong";
}

rendering::MaterialType materialTypeFromName(const std::string& name) {
    if (name == "Unlit") {
        return rendering::MaterialType::Unlit;
    }
    if (name == "PBR") {
        return rendering::MaterialType::PBR;
    }
    if (name == "UserType") {
        return rendering::MaterialType::UserType;
    }
    return rendering::MaterialType::BlinnPhong;
}

} // namespace

arti::asset::AssetType MaterialLoader::getType() const { return std::string{ kMaterialAssetType }; }

std::vector<std::byte> encodeMaterialArtifact(const MaterialAsset::Params& params) {
    YAML::Node node;
    node["Type"] = materialTypeName(params.type);
    node["BaseColor"] = std::vector{ params.base_color.r, params.base_color.g, params.base_color.b,
        params.base_color.a };
    node["SpecularColor"] = std::vector{ params.specular_color.r, params.specular_color.g,
        params.specular_color.b };
    node["SpecularStrength"] = params.specular_strength;
    node["Shininess"] = params.shininess;
    node["Metallic"] = params.metallic_strength;
    node["Roughness"] = params.roughness_strength;

    YAML::Emitter emitter;
    emitter << node;
    const std::string text = emitter.c_str();

    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

std::shared_ptr<arti::asset::Asset> MaterialLoader::decode(
        const arti::asset::AssetMetadata& metadata, const std::filesystem::path& artifact_file,
        std::span<const std::shared_ptr<arti::asset::Asset>> dependencies) {
    // 纹理还没做成资产，所以这一版没有依赖要消费。
    (void) dependencies;

    const YAML::Node node = YAML::Load(detail::readTextFile(artifact_file));

    MaterialAsset::Params params;
    // 逐字段带默认值地读：缺字段用结构体的默认值，不整体失败 ——
    // 这样以后加字段，旧 artifact 依然能读。
    if (node["Type"]) {
        params.type = materialTypeFromName(node["Type"].as<std::string>());
    }
    if (const auto base_color = node["BaseColor"]; base_color && base_color.size() == 4) {
        params.base_color = { base_color[0].as<float>(), base_color[1].as<float>(),
            base_color[2].as<float>(), base_color[3].as<float>() };
    }
    if (const auto specular = node["SpecularColor"]; specular && specular.size() == 3) {
        params.specular_color = { specular[0].as<float>(), specular[1].as<float>(),
            specular[2].as<float>() };
    }
    if (node["SpecularStrength"]) {
        params.specular_strength = node["SpecularStrength"].as<float>();
    }
    if (node["Shininess"]) {
        params.shininess = node["Shininess"].as<float>();
    }
    if (node["Metallic"]) {
        params.metallic_strength = node["Metallic"].as<float>();
    }
    if (node["Roughness"]) {
        params.roughness_strength = node["Roughness"].as<float>();
    }

    return std::make_shared<MaterialAsset>(metadata.handle, params);
}

} // namespace arti::engine::asset
