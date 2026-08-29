#pragma once
#include "arti_renderer.h"
#include "artichoco/core/layer.h"
#include "artichoco/core/timestep_accumulator.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace arti::renderer {
class RenderDevice;
} // namespace arti::renderer

namespace arti::rendering {
class Renderer;
} // namespace arti::rendering

namespace arti::scene {
class Scene;
class SceneSerializationRegistry;
class SceneSerializer;
} // namespace arti::scene

namespace arti::engine {
class ImGuiHost;
class RenderSceneExtractor;
} // namespace arti::engine

namespace arti::editor {

class EditorCamera;
class EditorGizmo;
class EditorProject;
class ContentBrowserPanel;
class HierarchyPanel;
class InspectorPanel;
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
    enum class Mode { Edit, Play };

    void createDefaultScene();
    void newProject();
    void openProject();
    // 打开项目后按 ProjectInfo::last_open_scene 恢复场景。返回是否真的读进来了 ——
    // 返回 false 的时候调用方要退回 createDefaultScene()。
    bool loadLastOpenScene();

    void newScene();
    void openScene();
    // path 为空时走 Save As（弹对话框）。返回是否真的存了。
    bool saveScene(const std::filesystem::path& path);
    void saveSceneAs();
    // 换场景前的公共动作：清实体、清选中、清 Play 状态。
    void resetSceneState();

    void drawMenuBar();
    void drawToolbar();
    void updateEditorCamera(float deltaTime);

    // 把 Content Browser 拖出来的资产在 Viewport 上放下来生成场景实体。
    void handleViewportAssetDrop(float rect_x, float rect_y, float rect_width, float rect_height);
    void spawnAssetEntity(core::UUID asset);

    void enterPlayMode();
    void exitPlayMode();

    // 当前场景文件。空 = 还没存过（Save 会转成 Save As）。
    std::filesystem::path m_scene_file;
    // 有未保存的改动。现在只在明确的编辑动作后置位，不做逐字段脏检查 ——
    // 那需要在 Inspector 和 gizmo 的每个写入点埋钩子，先做个够用的版本。
    bool m_scene_dirty{ false };

    Mode m_mode{ Mode::Edit };
    std::unique_ptr<renderer::RenderDevice> m_render_device;
    std::unique_ptr<rendering::Renderer> m_renderer;
    std::unique_ptr<scene::Scene> m_scene;
    std::unique_ptr<scene::Scene> m_snapshot;
    std::unique_ptr<engine::ImGuiHost> m_imgui;
    std::unique_ptr<engine::RenderSceneExtractor> m_extractor;

    core::FixedTimestepAccumulator m_fixed_accumulator;
    uint64_t m_play_frame_index{ 0 };

    std::unique_ptr<EditorProject> m_project;
    // 序列化表和 serializer。表不是进程级的（是个对象），所以编辑器自己持有一份。
    std::unique_ptr<scene::SceneSerializationRegistry> m_serialization;
    std::unique_ptr<scene::SceneSerializer> m_serializer;
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
