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
class SceneRenderer;
} // namespace arti::engine

namespace arti::editor {

class EditorCamera;
class EditorContext;
class EditorGizmo;
class EditorProject;
class ContentBrowserPanel;
class HierarchyPanel;
class InspectorPanel;
class ProjectSettingsPanel;
class SceneDocument;
class ViewportPanel;

class EditorLayer final : public core::Layer {
public:
    explicit EditorLayer(bool vsync = true);
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
    // 编辑器级的键盘快捷键（面板自己的快捷键归它自己，gizmo 的 W / E / R 在 EditorGizmo 里）。
    void handleShortcuts();
    void updateEditorCamera(float deltaTime);
    void submitSelectionGizmos();

    // 当前选中的实体能不能被复制 / 删除。菜单项的灰化和快捷键共用它们，免得两处判断跑偏。
    bool canDuplicateSelection() const;
    void duplicateSelection();
    bool canDeleteSelection() const;
    void deleteSelection();

    // 撤销 / 重做。同样是菜单项和快捷键共用一套前提 —— 除了「栈里有东西」，还要求不在模拟中、
    // 且没有正在进行的交互，理由见 .cpp 里 canEditHistory() 的注释。
    bool canEditHistory() const;
    bool canUndo() const;
    void undoEdit();
    bool canRedo() const;
    void redoEdit();

    // 场景文档那几个动作的前提。同样是菜单项的灰化和快捷键共用 —— 两处分别写会跑偏，
    // 而「菜单里是灰的、快捷键却能按」是最难查的那种不一致。
    bool canChangeScene() const;
    bool canSaveScene() const;

    // 把 Content Browser 拖出来的资产在 Viewport 上放下来生成场景实体。
    void handleViewportAssetDrop(float rect_x, float rect_y, float rect_width, float rect_height);
    void spawnAssetEntity(core::UUID asset);

    std::unique_ptr<renderer::RenderDevice> m_render_device;
    std::unique_ptr<rendering::Renderer> m_renderer;
    std::unique_ptr<engine::ImGuiHost> m_imgui;
    std::unique_ptr<engine::SceneRenderer> m_scene_renderer;

    std::unique_ptr<EditorProject> m_project;
    std::unique_ptr<EditorContext> m_context;
    std::unique_ptr<SceneDocument> m_document;
    std::unique_ptr<EditorCamera> m_editor_camera;
    std::unique_ptr<EditorGizmo> m_gizmo;
    std::unique_ptr<ContentBrowserPanel> m_content_browser_panel;
    std::unique_ptr<HierarchyPanel> m_hierarchy_panel;
    std::unique_ptr<InspectorPanel> m_inspector_panel;
    std::unique_ptr<ProjectSettingsPanel> m_project_settings_panel;
    std::unique_ptr<ViewportPanel> m_viewport_panel;

    uint32_t m_frame_index{ 0 };
    rendering::FrameStatistics m_last_statistics;
    bool m_vsync{ true };

    // 「一次交互刚结束」靠下降沿判：这两个存的是上一帧的状态。见 onImGuiRender() 帧末那段。
    bool m_was_item_active{ false };
    bool m_was_gizmo_using{ false };

    uint32_t m_viewport_width{ 0 };
    uint32_t m_viewport_height{ 0 };
};

} // namespace arti::editor
