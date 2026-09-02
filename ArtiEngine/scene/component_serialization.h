#pragma once
#include "artichoco/scene/component/component_serialization.h"
#include "artichoco/scene/scene_serialization_registry.h"
#include "scene/components.h"

namespace arti::engine {

class MeshRendererSerialization final
        : public scene::ComponentSerialization<MeshRendererComponent> {
public:
    static constexpr std::string_view typeName() noexcept { return "artiengine.mesh_renderer"; }

    YAML::Node serialize(const MeshRendererComponent& component) const override;
    MeshRendererComponent deserialize(const YAML::Node& node) const override;
};

class CameraSerialization final : public scene::ComponentSerialization<CameraComponent> {
public:
    static constexpr std::string_view typeName() noexcept { return "artiengine.camera"; }

    YAML::Node serialize(const CameraComponent& component) const override;
    CameraComponent deserialize(const YAML::Node& node) const override;
};

class DirectionalLightSerialization final
        : public scene::ComponentSerialization<DirectionalLightComponent> {
public:
    static constexpr std::string_view typeName() noexcept { return "artiengine.directional_light"; }

    YAML::Node serialize(const DirectionalLightComponent& component) const override;
    DirectionalLightComponent deserialize(const YAML::Node& node) const override;
};

class PointLightSerialization final : public scene::ComponentSerialization<PointLightComponent> {
public:
    static constexpr std::string_view typeName() noexcept { return "artiengine.point_light"; }

    YAML::Node serialize(const PointLightComponent& component) const override;
    PointLightComponent deserialize(const YAML::Node& node) const override;
};

class SpotLightSerialization final : public scene::ComponentSerialization<SpotLightComponent> {
public:
    static constexpr std::string_view typeName() noexcept { return "artiengine.spot_light"; }

    YAML::Node serialize(const SpotLightComponent& component) const override;
    SpotLightComponent deserialize(const YAML::Node& node) const override;
};

class EnvironmentSerialization final : public scene::ComponentSerialization<EnvironmentComponent> {
public:
    static constexpr std::string_view typeName() noexcept { return "artiengine.environment"; }

    YAML::Node serialize(const EnvironmentComponent& component) const override;
    EnvironmentComponent deserialize(const YAML::Node& node) const override;
};

class RigidBodySerialization final : public scene::ComponentSerialization<RigidBodyComponent> {
public:
    static constexpr std::string_view typeName() noexcept { return "artiengine.rigid_body"; }

    YAML::Node serialize(const RigidBodyComponent& component) const override;
    RigidBodyComponent deserialize(const YAML::Node& node) const override;
};

class ColliderSerialization final : public scene::ComponentSerialization<ColliderComponent> {
public:
    static constexpr std::string_view typeName() noexcept { return "artiengine.collider"; }

    YAML::Node serialize(const ColliderComponent& component) const override;
    ColliderComponent deserialize(const YAML::Node& node) const override;
};

void registerSceneSerialization(scene::SceneSerializationRegistry& registry);

}
