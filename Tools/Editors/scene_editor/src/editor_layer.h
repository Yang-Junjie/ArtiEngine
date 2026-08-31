#pragma once
#include "arti_renderer.h"
#include "artichoco/core/layer.h"

#include <cstdint>
#include <memory>

namespace arti::renderer {
class RenderDevice;
} // namespace arti::renderer

namespace arti::rendering {
class Renderer;
} // namespace arti::rendering

namespace arti::engine {
class ImGuiHost;
class RenderSceneExtractor;
} // namespace arti::engine

namespace arti::editor {

class EditorCamera;
class EditorContext;
class EditorGizmo;
class EditorProject;
class ContentBrowserPanel;
class HierarchyPanel;
class InspectorPanel;
class SceneDocument;
class ViewportPanel;

class EditorLayer final : public core::Layer {
public:
    explicit EditorLayer();
    ~EditorLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(core::Timestep deltaTime) override;
    void onImGuiRender() override;
    void onRender() override;

private:
    void newProject();
    void openProject();

    void drawMenuBar();
    void drawToolbar();
    void updateEditorCamera(float deltaTime);
    void submitSelectionGizmos();

    // 把 Content Browser 拖出来的资产在 Viewport 上放下来生成场景实体。
    void handleViewportAssetDrop(float rect_x, float rect_y, float rect_width, float rect_height);
    void spawnAssetEntity(core::UUID asset);

    std::unique_ptr<renderer::RenderDevice> m_render_device;
    std::unique_ptr<rendering::Renderer> m_renderer;
    std::unique_ptr<engine::ImGuiHost> m_imgui;
    std::unique_ptr<engine::RenderSceneExtractor> m_extractor;

    std::unique_ptr<EditorProject> m_project;
    std::unique_ptr<EditorContext> m_context;
    std::unique_ptr<SceneDocument> m_document;
    std::unique_ptr<EditorCamera> m_editor_camera;
    std::unique_ptr<EditorGizmo> m_gizmo;
    std::unique_ptr<ContentBrowserPanel> m_content_browser_panel;
    std::unique_ptr<HierarchyPanel> m_hierarchy_panel;
    std::unique_ptr<InspectorPanel> m_inspector_panel;
    std::unique_ptr<ViewportPanel> m_viewport_panel;

    uint32_t m_frame_index{ 0 };
    rendering::FrameStatistics m_last_statistics;

    uint32_t m_viewport_width{ 0 };
    uint32_t m_viewport_height{ 0 };
};

} // namespace arti::editor
