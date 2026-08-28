#pragma once
#include "artichoco/core/uuid.h"

#include <optional>

namespace arti::scene {
class Entity;
class Scene;
} // namespace arti::scene

namespace arti::editor {

// Inspector 面板：显示并编辑选中实体的组件。
class InspectorPanel {
public:
    explicit InspectorPanel(scene::Scene& scene);

    void draw(const std::optional<core::UUID>& selected_entity);

private:
    // 传 Entity 而不是 UUID：这几个函数都是在同一次 draw 里连着调的，
    // 每个都重新 findEntity 一遍没意义。
    void drawTagComponent(scene::Entity& entity);
    void drawTransformComponent(scene::Entity& entity);
    void drawCameraComponent(scene::Entity& entity);
    void drawMeshRendererComponent(scene::Entity& entity);
    void drawDirectionalLightComponent(scene::Entity& entity);

    scene::Scene& m_scene;
};

} // namespace arti::editor
