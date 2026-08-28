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
class HierarchyPanel;
class InspectorPanel;
class ViewportPanel;


class EditorLayer final : public core::Layer {
public:
    // frame_limit 非 0 时跑够帧数自动退出，用于冒烟测试。
    // auto_play 让它启动就进 Play 模式 —— 快照和恢复那条路才能被自动化覆盖。
    // auto_pick 每隔几帧往 Viewport 正中发一次拾取请求，覆盖 PickingPass 那条路。
    EditorLayer(const char* scene_path, uint32_t frame_limit = 0, bool auto_play = false,
            bool auto_pick = false);
    ~EditorLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(core::Timestep deltaTime) override;
    void onImGuiRender() override;
    void onRender() override;

private:
    // Edit：场景不动，EditorCamera 提供视角，游戏逻辑的 stage 不跑。
    // Play：游戏逻辑跑起来，视角来自场景的 CameraComponent。
    enum class Mode { Edit, Play };

    void createDefaultScene();
    void drawMenuBar();
    void drawToolbar();
    void updateEditorCamera(float deltaTime);

    void enterPlayMode();
    void exitPlayMode();

    std::string m_scene_path;
    Mode m_mode{ Mode::Edit };
    uint32_t m_frame_limit{ 0 };
    bool m_auto_play{ false };
    // 自动往 Viewport 正中发拾取请求，用于冒烟测试 —— 自动化跑的时候没人点鼠标，
    // 而不发请求 PickingPass 就整个跳过，shader 都不会编译，等于这条路没被测到。
    bool m_auto_pick{ false };

    std::unique_ptr<renderer::RenderDevice> m_render_device;
    std::unique_ptr<rendering::Renderer> m_renderer;
    std::unique_ptr<scene::Scene> m_scene;
    // 进 Play 之前把场景拷进来，Stop 时拷回去 —— 否则游戏逻辑改过的实体会留在编辑中的场景里，
    // 「跑一遍」就把编辑好的东西毁了。
    //
    // 用 Scene 而不是序列化成字节：SceneCloner 已经能干这事，而且它会拷 IDComponent，
    // 所以 UUID 跨快照不变 —— Stop 之后 Hierarchy 的选中还指向同一个实体。
    std::unique_ptr<scene::Scene> m_snapshot;
    std::unique_ptr<engine::ImGuiHost> m_imgui;
    std::unique_ptr<engine::RenderSceneExtractor> m_extractor;

    // FixedUpdate 用。Play 模式下按固定步长推进，一帧可能跑 0 到多次。
    core::FixedTimestepAccumulator m_fixed_accumulator;
    uint64_t m_play_frame_index{ 0 };

    std::unique_ptr<EditorCamera> m_editor_camera;
    std::unique_ptr<HierarchyPanel> m_hierarchy_panel;
    std::unique_ptr<InspectorPanel> m_inspector_panel;
    std::unique_ptr<ViewportPanel> m_viewport_panel;

    uint32_t m_frame_index{ 0 };
    rendering::FrameStatistics m_last_statistics;

    // Viewport 面板的尺寸 —— extract 算 aspect 的依据
    uint32_t m_viewport_width{ 0 };
    uint32_t m_viewport_height{ 0 };

    // 默认场景的资源。资产层做起来之后这些会从 AssetManager 来。
    rendering::MeshHandle m_cube_mesh;
    rendering::MaterialHandle m_default_material;
    rendering::TextureHandle m_checker_texture;
};

} // namespace arti::editor
