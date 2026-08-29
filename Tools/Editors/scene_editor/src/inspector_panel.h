#pragma once
#include "artichoco/core/uuid.h"

#include <optional>

namespace arti::scene {
class Entity;
class Scene;
} // namespace arti::scene

namespace arti::editor {

class InspectorPanel {
public:
    explicit InspectorPanel(scene::Scene& scene);

    void draw(const std::optional<core::UUID>& selected_entity);

private:
    void drawTagComponent(scene::Entity& entity);
    void drawTransformComponent(scene::Entity& entity);
    void drawCameraComponent(scene::Entity& entity);
    void drawMeshRendererComponent(scene::Entity& entity);
    void drawDirectionalLightComponent(scene::Entity& entity);
    void drawEnvironmentComponent(scene::Entity& entity);

    scene::Scene& m_scene;
};

} // namespace arti::editor
