#include "scene/component_serialization.h"

#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "asset/texture_asset.h"

#include <stdexcept>

namespace arti::engine {
namespace {

YAML::Node writeUUID(core::UUID id) {
    return YAML::Node(id.isValid() ? id.toString() : std::string{});
}

core::UUID readUUID(const YAML::Node& node) {
    if (!node || !node.IsScalar()) {
        return {};
    }
    const auto text = node.as<std::string>();
    if (text.empty()) {
        return {};
    }
    const auto parsed = core::UUID::fromString(text);
    return parsed ? *parsed : core::UUID{};
}

YAML::Node writeVector3(const glm::vec3& value) {
    YAML::Node node(YAML::NodeType::Sequence);
    node.push_back(value.x);
    node.push_back(value.y);
    node.push_back(value.z);
    node.SetStyle(YAML::EmitterStyle::Flow);
    return node;
}

glm::vec3 readVector3(const YAML::Node& node, const glm::vec3& fallback) {
    if (!node || !node.IsSequence() || node.size() != 3) {
        return fallback;
    }
    return glm::vec3{ node[0].as<float>(), node[1].as<float>(), node[2].as<float>() };
}

void requireMap(const YAML::Node& node, const char* what) {
    if (!node || !node.IsMap()) {
        throw std::invalid_argument(std::string{ what } + " component data must be a YAML map.");
    }
}

}

YAML::Node MeshRendererSerialization::serialize(const MeshRendererComponent& component) const {
    YAML::Node node;
    node["Mesh"] = writeUUID(component.mesh.id());

    YAML::Node materials(YAML::NodeType::Sequence);
    for (const auto& material: component.materials) {
        materials.push_back(writeUUID(material.id()));
    }
    materials.SetStyle(YAML::EmitterStyle::Flow);
    node["Materials"] = materials;

    node["Visible"] = component.visible;
    return node;
}

MeshRendererComponent MeshRendererSerialization::deserialize(const YAML::Node& node) const {
    requireMap(node, "MeshRenderer");

    MeshRendererComponent component;
    component.mesh = arti::asset::AssetHandle<asset::MeshAsset>{ readUUID(node["Mesh"]) };

    if (const auto materials = node["Materials"]; materials && materials.IsSequence()) {
        component.materials.reserve(materials.size());
        for (const auto& material: materials) {
            component.materials.emplace_back(readUUID(material));
        }
    }

    if (node["Visible"]) {
        component.visible = node["Visible"].as<bool>();
    }
    return component;
}

YAML::Node CameraSerialization::serialize(const CameraComponent& component) const {
    YAML::Node node;
    node["FovDegrees"] = component.fov_degrees;
    node["NearPlane"] = component.near_plane;
    node["FarPlane"] = component.far_plane;
    node["Primary"] = component.primary;
    return node;
}

CameraComponent CameraSerialization::deserialize(const YAML::Node& node) const {
    requireMap(node, "Camera");

    CameraComponent component;
    if (node["FovDegrees"]) {
        component.fov_degrees = node["FovDegrees"].as<float>();
    }
    if (node["NearPlane"]) {
        component.near_plane = node["NearPlane"].as<float>();
    }
    if (node["FarPlane"]) {
        component.far_plane = node["FarPlane"].as<float>();
    }
    if (node["Primary"]) {
        component.primary = node["Primary"].as<bool>();
    }
    return component;
}

YAML::Node DirectionalLightSerialization::serialize(
        const DirectionalLightComponent& component) const {
    YAML::Node node;
    node["Color"] = writeVector3(component.color);
    node["Intensity"] = component.intensity;
    node["Enabled"] = component.enabled;
    return node;
}

DirectionalLightComponent DirectionalLightSerialization::deserialize(const YAML::Node& node) const {
    requireMap(node, "DirectionalLight");

    DirectionalLightComponent component;
    component.color = readVector3(node["Color"], component.color);
    if (node["Intensity"]) {
        component.intensity = node["Intensity"].as<float>();
    }
    if (node["Enabled"]) {
        component.enabled = node["Enabled"].as<bool>();
    }
    return component;
}

YAML::Node PointLightSerialization::serialize(const PointLightComponent& component) const {
    YAML::Node node;
    node["Color"] = writeVector3(component.color);
    node["Intensity"] = component.intensity;
    node["Range"] = component.range;
    node["Enabled"] = component.enabled;
    return node;
}

PointLightComponent PointLightSerialization::deserialize(const YAML::Node& node) const {
    requireMap(node, "PointLight");

    PointLightComponent component;
    component.color = readVector3(node["Color"], component.color);
    if (node["Intensity"]) {
        component.intensity = node["Intensity"].as<float>();
    }
    if (node["Range"]) {
        component.range = node["Range"].as<float>();
    }
    if (node["Enabled"]) {
        component.enabled = node["Enabled"].as<bool>();
    }
    return component;
}

YAML::Node SpotLightSerialization::serialize(const SpotLightComponent& component) const {
    YAML::Node node;
    node["Color"] = writeVector3(component.color);
    node["Intensity"] = component.intensity;
    node["Range"] = component.range;
    node["InnerConeDegrees"] = component.inner_cone_degrees;
    node["OuterConeDegrees"] = component.outer_cone_degrees;
    node["Enabled"] = component.enabled;
    return node;
}

SpotLightComponent SpotLightSerialization::deserialize(const YAML::Node& node) const {
    requireMap(node, "SpotLight");

    SpotLightComponent component;
    component.color = readVector3(node["Color"], component.color);
    if (node["Intensity"]) {
        component.intensity = node["Intensity"].as<float>();
    }
    if (node["Range"]) {
        component.range = node["Range"].as<float>();
    }
    if (node["InnerConeDegrees"]) {
        component.inner_cone_degrees = node["InnerConeDegrees"].as<float>();
    }
    if (node["OuterConeDegrees"]) {
        component.outer_cone_degrees = node["OuterConeDegrees"].as<float>();
    }
    if (node["Enabled"]) {
        component.enabled = node["Enabled"].as<bool>();
    }
    return component;
}

YAML::Node EnvironmentSerialization::serialize(const EnvironmentComponent& component) const {
    YAML::Node node;
    node["EquirectTexture"] = writeUUID(component.equirect_texture.id());
    node["SkyColor"] = writeVector3(component.sky_color);
    node["Intensity"] = component.intensity;
    node["Enabled"] = component.enabled;
    node["SkyVisible"] = component.sky_visible;
    return node;
}

EnvironmentComponent EnvironmentSerialization::deserialize(const YAML::Node& node) const {
    requireMap(node, "Environment");

    EnvironmentComponent component;
    component.equirect_texture =
            arti::asset::AssetHandle<asset::TextureAsset>{ readUUID(node["EquirectTexture"]) };
    component.sky_color = readVector3(node["SkyColor"], component.sky_color);
    if (node["Intensity"]) {
        component.intensity = node["Intensity"].as<float>();
    }
    if (node["Enabled"]) {
        component.enabled = node["Enabled"].as<bool>();
    }
    if (node["SkyVisible"]) {
        component.sky_visible = node["SkyVisible"].as<bool>();
    }
    return component;
}

void registerSceneSerialization(scene::SceneSerializationRegistry& registry) {
    registry.registerComponent<MeshRendererComponent>(
            std::string{ MeshRendererSerialization::typeName() },
            std::make_unique<MeshRendererSerialization>());
    registry.registerComponent<CameraComponent>(std::string{ CameraSerialization::typeName() },
            std::make_unique<CameraSerialization>());
    registry.registerComponent<DirectionalLightComponent>(
            std::string{ DirectionalLightSerialization::typeName() },
            std::make_unique<DirectionalLightSerialization>());
    registry.registerComponent<PointLightComponent>(
            std::string{ PointLightSerialization::typeName() },
            std::make_unique<PointLightSerialization>());
    registry.registerComponent<SpotLightComponent>(
            std::string{ SpotLightSerialization::typeName() },
            std::make_unique<SpotLightSerialization>());
    registry.registerComponent<EnvironmentComponent>(
            std::string{ EnvironmentSerialization::typeName() },
            std::make_unique<EnvironmentSerialization>());
}

}
