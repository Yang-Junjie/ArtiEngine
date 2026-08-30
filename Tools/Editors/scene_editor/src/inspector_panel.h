#pragma once

namespace arti::scene {
class Entity;
} // namespace arti::scene

namespace arti::editor {

class EditorContext;

class InspectorPanel {
public:
    explicit InspectorPanel(EditorContext& context);

    void draw();

private:
    void drawTagComponent(scene::Entity& entity);
    void drawTransformComponent(scene::Entity& entity);
    void drawCameraComponent(scene::Entity& entity);
    void drawMeshRendererComponent(scene::Entity& entity);
    void drawDirectionalLightComponent(scene::Entity& entity);
    void drawEnvironmentComponent(scene::Entity& entity);

    EditorContext& m_context;
};

} // namespace arti::editor
