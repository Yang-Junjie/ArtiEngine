#pragma once
#include "arti_renderer.h"
#include "artichoco/core/layer.h"
#include "artichoco/core/timestep_accumulator.h"

#include <cstdint>
#include <filesystem>
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

// 命令行开关的集合。做成 struct 而不是继续往构造函数上加参数：原来已经是四个连续的 bool，
// 再加就是在赌调用方不会写错顺序，而写错了编译器一句话都不会说。
struct EditorLayerOptions {
    const char* scene_path{ nullptr };
    // 打开这个已有项目而不是在临时目录里建一个。为空时才看 auto_project。
    std::filesystem::path project_file;
    // 把这个「项目相对的源文件路径」对应的已导入贴图指给场景的 EnvironmentComponent。
    // 自动化验证 IBL 用 —— 手工操作是在 Inspector 里填 UUID，自动化点不了。
    std::filesystem::path environment_source;
    uint32_t frame_limit{ 0 };
    bool auto_play{ false };
    bool auto_pick{ false };
    bool auto_project{ false };
    // 存一次场景、清空、再读回来，然后比对实体和组件。用于冒烟测试 ——
    // 存读是菜单驱动的，自动化点不了对话框，而不覆盖的话「存了但读不回来」不会被发现。
    bool auto_scene_io{ false };
};

class EditorLayer final : public core::Layer {
public:
    explicit EditorLayer(EditorLayerOptions options);
    ~EditorLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(core::Timestep deltaTime) override;
    void onImGuiRender() override;
    void onRender() override;

private:
    enum class Mode { Edit, Play };

    void createDefaultScene();
    // 把 options.environment_source 指向的贴图指给场景里的 EnvironmentComponent（没有就建一个）。
    void applyEnvironmentOverride();
    void newProject();
    void openProject();

    void newScene();
    void openScene();
    // path 为空时走 Save As（弹对话框）。返回是否真的存了。
    bool saveScene(const std::filesystem::path& path);
    void saveSceneAs();
    // 换场景前的公共动作：清实体、清选中、清 Play 状态。
    void resetSceneState();
    // 冒烟测试用：存 -> 清 -> 读 -> 比对，结果打进日志。
    void runSceneIoCheck();

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

    std::string m_scene_path;
    Mode m_mode{ Mode::Edit };
    EditorLayerOptions m_options;

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
