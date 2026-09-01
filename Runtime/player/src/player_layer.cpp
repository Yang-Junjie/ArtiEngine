#include "player_layer.h"

#include "asset/gpu_asset_cache.h"
#include "imgui/imgui_host.h"
#include "runtime/asset_runtime.h"
#include "runtime/scene_renderer.h"
#include "runtime/world.h"

#include "artichoco/core/application.h"
#include "artichoco/core/io/input.h"
#include "artichoco/platform/window/sdl_vulkan_surface_source.h"
#include "artichoco/project/project_manager.h"
#include "artichoco/renderer/render_device.h"
#include "artichoco/scene/components.h"
#include "artichoco/scene/scene.h"

#include <imgui.h>

#include <stdexcept>
#include <utility>

namespace arti::player {
namespace {

const core::Logger::Channel& log() { return core::Application::get().getLogChannel(); }

} // namespace

PlayerLayer::PlayerLayer(PlayerOptions options)
        : Layer("PlayerLayer"),
          m_options(std::move(options)) {}

PlayerLayer::~PlayerLayer() = default;

void PlayerLayer::onAttach() {
    auto& app = core::Application::get();

    auto surface_source = platform::createSDLVulkanSurfaceSource(app.getWindow());
    renderer::RenderDeviceCreateInfo device_info;
    device_info.application_name = "ArtiEngine Player";
    m_render_device = std::make_unique<renderer::RenderDevice>(app.getWindow(),
            std::move(surface_source), device_info);

    rendering::RendererCreateInfo renderer_info;
    // Direct: PresentPass 把场景贴到 backbuffer。编辑器那边是 IntoUI，因为场景要进面板。
    renderer_info.present = rendering::PresentMode::Direct;
    m_renderer = std::make_unique<rendering::Renderer>(*m_render_device, renderer_info);

    m_assets = std::make_unique<engine::AssetRuntime>();
    m_world = std::make_unique<engine::World>();
    m_scene_renderer = std::make_unique<engine::SceneRenderer>(*m_renderer);

    if (m_options.show_stats) {
        engine::ImGuiHostCreateInfo imgui_info;
        // 播放器只有一个自绘的小覆盖层：不要停靠，也别把布局写到工作目录去。
        imgui_info.docking = false;
        imgui_info.persist_layout = false;
        // font_path 留空 = 用 ImGui 内建位图字体。HUD 全是 ASCII，没必要带一份 TTF。
        m_imgui = std::make_unique<engine::ImGuiHost>(app.getWindow(), *m_renderer, imgui_info);
    }

    // 参数和路径在 createApplication 里就查过了，走到这里还会失败只剩「文件本身坏了」：
    // manifest 解析不过、artifact 目录不在、场景 YAML 损坏。
    //
    // 抛而不是 app.close()：close() 只会让 run() 立刻返回，进程退出码仍然是 0，脚本和 CI
    // 分不出「跑完了」和「压根没起来」。抛出去由 entry_point 接住，它会记一条 fatal、
    // 往 stderr 写原因，并返回 EXIT_FAILURE。
    //
    // 此刻还没提交过任何一帧，所以先手动走一遍 onDetach() 把设备按顺序拆掉就够了 ——
    // 这一层还没进 LayerStack（pushLayer 是先 onAttach 再入栈），不会有人再替我们拆。
    if (!openProject() || !m_world->loadScene(m_options.scene_file)) {
        onDetach();
        throw std::runtime_error("The player failed to load the project or its start scene; "
                                 "see the log for the reason.");
    }

    m_ready = true;
    const auto output = m_renderer->outputInfo();
    log().info("Player ready: project '{}', output {}x{}", m_options.project_file.string(),
            output.width, output.height);
}

void PlayerLayer::onDetach() {
    if (m_renderer) {
        m_renderer->waitIdle();
    }
    m_imgui.reset();
    m_scene_renderer.reset();
    m_world.reset();
    // GPU cache 先于 AssetRuntime: 它握着从 AssetManager 加载出来的资产。
    m_gpu_assets.reset();
    m_assets.reset();
    m_renderer.reset();
    m_render_device.reset();
}

bool PlayerLayer::openProject() {
    // 项目文件已经由 createApplication 加载进 ProjectManager 了（窗口标题要用项目名）。
    auto& projects = project::ProjectManager::instance();
    const auto artifacts_root = projects.getArtifactsRootPath();
    if (!artifacts_root) {
        log().error("The project has no artifacts root");
        return false;
    }

    // 两条路都不 reconcile: 导入是编辑期的事，运行时连 importer 都没注册。资产缺失会在
    // 加载时逐个报错，而不是在这里悄悄把源文件重导一遍。
    if (!m_options.manifest_file.empty()) {
        // 打包模式：catalog 从 manifest 建，Assets/ 完全不参与。
        if (!m_assets->openPackaged(*artifacts_root, m_options.manifest_file)) {
            return false;
        }
        log().info("Opened the packaged asset workspace ({})",
                m_options.manifest_file.filename().string());
    } else {
        const auto assets_root = projects.getAssetsRootPath();
        if (!assets_root) {
            log().error("The project has no assets root");
            return false;
        }
        if (!m_assets->open(*assets_root, *artifacts_root)) {
            return false;
        }
        log().info("Opened the development asset workspace (scanning .meta under Assets/)");
    }

    m_gpu_assets = std::make_unique<engine::asset::GPUAssetCache>(m_assets->manager(), *m_renderer);
    return true;
}

void PlayerLayer::onUpdate(core::Timestep delta_time) {
    ++m_frame_index;
    if (!m_ready) {
        return;
    }

    // Esc 退出。播放器没有菜单，除了窗口的关闭按钮之外总得有一条键盘上的出路。
    if (core::Input::isKeyPressed(core::KeyCode::Escape)) {
        core::Application::get().close();
        return;
    }

    m_world->tick(delta_time.getSeconds());
}

void PlayerLayer::onImGuiRender() {
    if (!m_imgui) {
        return;
    }

    m_imgui->beginFrame();
    drawStats();
    m_imgui->endFrame();
}

void PlayerLayer::onRender() {
    // submit() 必须每帧都到：它是唯一提交 HUD draw data 的地方，跳过就是黑屏。
    if (!m_renderer || !m_scene_renderer || !m_world) {
        return;
    }

    const auto output = m_renderer->outputInfo();

    engine::SceneRenderer::ViewportInfo viewport;
    // 宽高比从 outputInfo 取，渲染目标本身跟着输出走 —— 见 ViewportInfo 的注释。
    viewport.width = output.width;
    viewport.height = output.height;
    viewport.target_follows_output = true;
    // 没有覆盖相机：运行时的相机就是场景里那个 primary CameraComponent。

    m_scene_renderer->prepare(m_world->scene(), m_ready ? m_gpu_assets.get() : nullptr, viewport);
    m_last_statistics =
            m_scene_renderer->submit(m_imgui ? m_imgui->overlay() : rendering::FrameOverlay{});

    if (m_frame_index == 1 && m_last_statistics.rendered) {
        log().info("First frame rendered ({} draw calls)", m_last_statistics.draw_calls);
    }
}

void PlayerLayer::drawStats() {
    constexpr float kPadding = 10.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
            ImVec2{ viewport->WorkPos.x + kPadding, viewport->WorkPos.y + kPadding },
            ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);

    constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (!ImGui::Begin("Stats", nullptr, kFlags)) {
        ImGui::End();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("%.1f FPS (%.2f ms)", io.Framerate,
            io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
    ImGui::Text("%u draws / %u submeshes / %u culled", m_last_statistics.draw_calls,
            m_last_statistics.submeshes, m_last_statistics.culled);

    if (m_world) {
        const auto entities = m_world->scene().view<scene::IDComponent>().size();
        ImGui::Text("%zu entities | frame %llu", static_cast<size_t>(entities),
                static_cast<unsigned long long>(m_world->frameIndex()));
    }

    // 场景里没有 primary 相机时画面是全黑的，而黑屏看不出原因 —— 这行就是原因。
    if (m_ready && !m_scene_renderer->hasCamera()) {
        ImGui::TextColored(ImVec4{ 1.0f, 0.4f, 0.3f, 1.0f }, "no primary camera in the scene");
    }

    ImGui::End();
}

} // namespace arti::player
