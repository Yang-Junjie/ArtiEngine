#pragma once
#include "arti_renderer.h"
#include "artichoco/core/layer.h"
#include "artichoco/core/timestep_accumulator.h"

#include <cstdint>
#include <memory>
#include <string>

namespace arti::renderer {
class RenderDevice;
} // namespace arti::renderer

namespace arti::rendering {
class Renderer;
} // namespace arti::rendering

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::engine {
class ImGuiHost;
class RenderSceneExtractor;
} // namespace arti::engine

namespace arti::editor {

class EditorCamera;
class EditorGizmo;
class EditorProject;
class HierarchyPanel;
class InspectorPanel;
class ViewportPanel;

class EditorLayer final : public core::Layer {
public:
    EditorLayer(const char* scene_path, uint32_t frame_limit = 0, bool auto_play = false,
            bool auto_pick = false, bool auto_project = false);
    ~EditorLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(core::Timestep deltaTime) override;
    void onImGuiRender() override;
    void onRender() override;

private:
    enum class Mode { Edit, Play };

    void createDefaultScene();
    void newProject();
    void openProject();
    void drawMenuBar();
    void drawToolbar();
    void updateEditorCamera(float deltaTime);

    void enterPlayMode();
    void exitPlayMode();

    std::string m_scene_path;
    Mode m_mode{ Mode::Edit };
    uint32_t m_frame_limit{ 0 };
    bool m_auto_play{ false };
    bool m_auto_pick{ false };
    bool m_auto_project{ false };

    std::unique_ptr<renderer::RenderDevice> m_render_device;
    std::unique_ptr<rendering::Renderer> m_renderer;
    std::unique_ptr<scene::Scene> m_scene;
    std::unique_ptr<scene::Scene> m_snapshot;
    std::unique_ptr<engine::ImGuiHost> m_imgui;
    std::unique_ptr<engine::RenderSceneExtractor> m_extractor;

    core::FixedTimestepAccumulator m_fixed_accumulator;
    uint64_t m_play_frame_index{ 0 };

    std::unique_ptr<EditorProject> m_project;
    std::unique_ptr<EditorCamera> m_editor_camera;
    std::unique_ptr<EditorGizmo> m_gizmo;
    std::unique_ptr<HierarchyPanel> m_hierarchy_panel;
    std::unique_ptr<InspectorPanel> m_inspector_panel;
    std::unique_ptr<ViewportPanel> m_viewport_panel;

    uint32_t m_frame_index{ 0 };
    rendering::FrameStatistics m_last_statistics;

    uint32_t m_viewport_width{ 0 };
    uint32_t m_viewport_height{ 0 };
};

} // namespace arti::editor
