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

}

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
    node["Occlusion"] = params.occlusion_strength;
    node["Emissive"] = params.emissive_strength;

    if (params.base_color_texture.isValid()) {
        node["BaseColorTexture"] = params.base_color_texture.id().toString();
    }
    if (params.metallic_roughness_texture.isValid()) {
        node["MetallicRoughnessTexture"] = params.metallic_roughness_texture.id().toString();
    }
    if (params.normal_texture.isValid()) {
        node["NormalTexture"] = params.normal_texture.id().toString();
    }
    if (params.occlusion_texture.isValid()) {
        node["OcclusionTexture"] = params.occlusion_texture.id().toString();
    }
    if (params.emissive_texture.isValid()) {
        node["EmissiveTexture"] = params.emissive_texture.id().toString();
    }

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
    const YAML::Node node = YAML::Load(detail::readTextFile(artifact_file));

    MaterialAsset::Params params;
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
    if (node["Occlusion"]) {
        params.occlusion_strength = node["Occlusion"].as<float>();
    }
    if (node["Emissive"]) {
        params.emissive_strength = node["Emissive"].as<float>();
    }

    const auto read_texture = [&node](const char* key) {
        arti::asset::AssetHandle<TextureAsset> handle;
        if (const auto value = node[key]) {
            if (const auto parsed = core::UUID::fromString(value.as<std::string>())) {
                handle = arti::asset::AssetHandle<TextureAsset>{ *parsed };
            }
        }
        return handle;
    };
    params.base_color_texture = read_texture("BaseColorTexture");
    params.metallic_roughness_texture = read_texture("MetallicRoughnessTexture");
    params.normal_texture = read_texture("NormalTexture");
    params.occlusion_texture = read_texture("OcclusionTexture");
    params.emissive_texture = read_texture("EmissiveTexture");

    // 持住 AssetManager 已经递归加载好的依赖。artifact 里存的是 UUID，
    // 但 AssetManager::m_loaded 是 weak_ptr —— 不持引用的话，材质手里的
    // UUID 可能指向一个已被回收的纹理。纹理跨源共享之后这一点尤其要紧。
    std::vector<std::shared_ptr<arti::asset::Asset>> retained{ dependencies.begin(),
        dependencies.end() };
    return std::make_shared<MaterialAsset>(metadata.handle, params, std::move(retained));
}

}
