#pragma once
#include "arti_renderer.h"
#include "artichoco/core/layer.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace arti::renderer {
class RenderDevice;
} // namespace arti::renderer

namespace arti::rendering {
class Renderer;
} // namespace arti::rendering

namespace arti::engine {
class AssetRuntime;
class ImGuiHost;
class SceneRenderer;
class World;
namespace asset {
class GPUAssetCache;
} // namespace asset
} // namespace arti::engine

namespace arti::player {

struct PlayerOptions {
    // 已经由 createApplication 加载进 ProjectManager 的项目文件。留着只为日志。
    std::filesystem::path project_file;
    // 要跑的场景，绝对路径，createApplication 已经确认它存在 —— 「用 StartScene 还是用
    // --scene」和「文件在不在」都在建渲染设备之前就定下来了。
    std::filesystem::path scene_file;

    // 打包产物里的 catalog manifest。非空 = 打包模式：catalog 从它建，Assets/ 不参与。
    // 空 = 开发模式，catalog 靠扫 Assets/ 下的 .meta。由 createApplication 按项目根下有没有
    // catalog.artimanifest 自动判定 —— 发布出来的目录里有，开发中的项目里没有。
    std::filesystem::path manifest_file;

    bool show_stats{ false };
    bool vsync{ true };
};

// 独立播放器的唯一一层：建渲染设备、开资产工作区、加载起始场景，然后每帧 tick + 提交。
//
// 真正干活的三个东西（World / AssetRuntime / SceneRenderer）都在 ArtiEngine::Runtime 里，
// 编辑器的 Play 模式消费的是同一份。这一层只有胶水：谁先建、谁后拆、参数从哪来。
class PlayerLayer final : public core::Layer {
public:
    explicit PlayerLayer(PlayerOptions options);
    ~PlayerLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(core::Timestep delta_time) override;
    void onImGuiRender() override;
    void onRender() override;

private:
    bool openProject();
    void drawStats();

    PlayerOptions m_options;

    std::unique_ptr<renderer::RenderDevice> m_render_device;
    std::unique_ptr<rendering::Renderer> m_renderer;
    std::unique_ptr<engine::AssetRuntime> m_assets;
    std::unique_ptr<engine::asset::GPUAssetCache> m_gpu_assets;
    std::unique_ptr<engine::World> m_world;
    std::unique_ptr<engine::SceneRenderer> m_scene_renderer;
    // 只有 --stats 时才建。默认不建：运行时不该为了「可能要画点调试信息」就常驻一个 UI 上下文。
    std::unique_ptr<engine::ImGuiHost> m_imgui;

    rendering::FrameStatistics m_last_statistics;
    // 项目和场景都就位了。onAttach 里任何一步失败就保持 false 并请求关闭应用 ——
    // 一个加载不了场景的播放器该带着原因退出，而不是留一个永远黑着的窗口。
    bool m_ready{ false };
    std::uint32_t m_frame_index{ 0 };
};

} // namespace arti::player
