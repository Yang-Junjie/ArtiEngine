#include "editor_layer.h"

#include "editor_camera.h"
#include "hierarchy_panel.h"
#include "inspector_panel.h"
#include "viewport_panel.h"

#include "arti_engine.h"
#include "imgui/imgui_host.h"

#include "artichoco/core/application.h"
#include "artichoco/platform/window/sdl_vulkan_surface_source.h"
#include "artichoco/renderer/render_device.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <array>
#include <cstddef>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <span>
#include <utility>
#include <vector>

namespace arti::editor {
namespace {

// 立方体：每个面 4 个顶点，方便给独立的法线和 UV。
// 三角形按「从外面看逆时针」编写，与 ArtiRenderer 的 opaque pass 的正面约定一致。
rendering::Mesh makeCubeMesh() {
    struct FaceDesc {
        glm::vec3 normal;
        glm::vec3 origin;
        glm::vec3 right;
        glm::vec3 up;
    };

    constexpr std::array<FaceDesc, 6> faces{ {
        { { 1, 0, 0 }, { 1, -1, 1 }, { 0, 0, -2 }, { 0, 2, 0 } },
        { { -1, 0, 0 }, { -1, -1, -1 }, { 0, 0, 2 }, { 0, 2, 0 } },
        { { 0, 1, 0 }, { -1, 1, 1 }, { 2, 0, 0 }, { 0, 0, -2 } },
        { { 0, -1, 0 }, { -1, -1, -1 }, { 2, 0, 0 }, { 0, 0, 2 } },
        { { 0, 0, 1 }, { -1, -1, 1 }, { 2, 0, 0 }, { 0, 2, 0 } },
        { { 0, 0, -1 }, { 1, -1, -1 }, { -2, 0, 0 }, { 0, 2, 0 } },
    } };

    rendering::Mesh mesh;
    mesh.vertices.reserve(faces.size() * 4);
    mesh.indices.reserve(faces.size() * 6);

    for (const auto& face: faces) {
        const auto base = static_cast<uint32_t>(mesh.vertices.size());
        const std::array<glm::vec2, 4> uvs{ { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f },
            { 0.0f, 0.0f } } };
        const std::array<glm::vec3, 4> corners{ {
            face.origin,
            face.origin + face.right,
            face.origin + face.right + face.up,
            face.origin + face.up,
        } };

        for (size_t corner = 0; corner < corners.size(); ++corner) {
            rendering::MeshVertex vertex;
            vertex.position = corners[corner] * 0.5f;
            vertex.normal = face.normal;
            vertex.tangent = glm::normalize(face.right);
            vertex.bitangent = glm::normalize(face.up);
            vertex.uv = uvs[corner];
            mesh.vertices.push_back(vertex);
            mesh.bounds.expand(vertex.position);
        }

        for (const uint32_t offset: { 0U, 1U, 2U, 0U, 2U, 3U }) {
            mesh.indices.push_back(base + offset);
        }
    }

    return mesh;
}

} // namespace

EditorLayer::EditorLayer(const char* scene_path, uint32_t frame_limit, bool auto_play,
        bool auto_pick)
        : Layer("EditorLayer"),
          m_scene_path(scene_path ? scene_path : ""),
          m_frame_limit(frame_limit),
          m_auto_play(auto_play),
          m_auto_pick(auto_pick) {}

EditorLayer::~EditorLayer() = default;

void EditorLayer::onAttach() {
    auto& app = core::Application::get();

    auto surface_source = platform::createSDLVulkanSurfaceSource(app.getWindow());
    renderer::RenderDeviceCreateInfo device_info;
    device_info.application_name = "ArtiEngine Scene Editor";
    m_render_device = std::make_unique<renderer::RenderDevice>(app.getWindow(),
            std::move(surface_source), device_info);

    // 编辑器固定 IntoUI 模式：场景画进 Viewport 面板
    rendering::RendererCreateInfo renderer_info;
    renderer_info.present = rendering::PresentMode::IntoUI;
    m_renderer = std::make_unique<rendering::Renderer>(*m_render_device, renderer_info);

    // 必须在克隆任何场景之前调：没注册的组件在 SceneCloner 里会被跳过，
    // 表现就是「按一次 Play，网格和光照全没了」。
    engine::registerSceneComponents();

    m_scene = std::make_unique<scene::Scene>();
    m_snapshot = std::make_unique<scene::Scene>();
    m_extractor = std::make_unique<engine::RenderSceneExtractor>();
    m_editor_camera = std::make_unique<EditorCamera>();

    engine::ImGuiHostCreateInfo imgui_info;
    imgui_info.persist_layout = true;
    imgui_info.docking = true;
    m_imgui = std::make_unique<engine::ImGuiHost>(app.getWindow(), *m_renderer, imgui_info);

    // UI 面板
    m_hierarchy_panel = std::make_unique<HierarchyPanel>(*m_scene);
    m_inspector_panel = std::make_unique<InspectorPanel>(*m_scene);
    m_viewport_panel = std::make_unique<ViewportPanel>(*m_renderer);

    // 加载场景或创建默认场景
    if (!m_scene_path.empty()) {
        // TODO: 序列化后从文件加载
        app.getLogChannel().warn("Scene serialization not implemented yet, using default scene");
        createDefaultScene();
    } else {
        createDefaultScene();
    }

    const auto output = m_renderer->outputInfo();
    app.getLogChannel().info("Scene Editor ready, output {}x{}", output.width, output.height);
}

void EditorLayer::onDetach() {
    if (m_renderer) {
        m_renderer->waitIdle();
    }
    m_viewport_panel.reset();
    m_inspector_panel.reset();
    m_hierarchy_panel.reset();
    m_imgui.reset();
    m_editor_camera.reset();
    m_extractor.reset();
    m_snapshot.reset();
    m_scene.reset();
    m_renderer.reset();
    m_render_device.reset();
}

void EditorLayer::onUpdate(core::Timestep deltaTime) {
    ++m_frame_index;
    if (!m_scene) {
        return;
    }

    // 冒烟测试：跑一半切进 Play，这样快照和恢复两条路在一次运行里都被走到。
    if (m_auto_play && m_frame_limit != 0 && m_mode == Mode::Edit &&
            m_frame_index >= m_frame_limit / 2) {
        enterPlayMode();
    }
    if (m_frame_limit != 0 && m_frame_index >= m_frame_limit) {
        if (m_mode == Mode::Play) {
            exitPlayMode();
        }
        core::Application::get().getLogChannel().info("Frame limit reached after {} frames",
                m_frame_index);
        core::Application::get().close();
        return;
    }

    const float dt = deltaTime.getSeconds();

    if (m_mode == Mode::Edit) {
        // Edit 模式**不跑** FixedUpdate / Update / LateUpdate —— 这就是「暂停」的全部实现。
        //
        // 刻意不用 setSystemEnabled()：那个是模板，要编译期知道每个系统的类型，没法表达
        // 「所有游戏逻辑系统」。stage 本身就是这个开关，而且对以后新加的系统自动生效。
        updateEditorCamera(dt);
        return;
    }

    scene::UpdateContext context;
    context.deltaTime = dt;
    context.fixedDeltaTime = m_fixed_accumulator.fixedDeltaTime();
    context.frameIndex = m_play_frame_index++;

    // FixedUpdate 可能一帧跑 0 次或多次（accumulator 内部对 real_delta 有上限，
    // 所以卡一下之后不会陷进补帧的死循环）。
    m_fixed_accumulator.tick(dt, [this, &context](float fixed_dt) {
        context.fixedDeltaTime = fixed_dt;
        m_scene->runSystems(scene::SystemStage::FixedUpdate, context);
    });

    m_scene->runSystems(scene::SystemStage::Update, context);
    m_scene->runSystems(scene::SystemStage::LateUpdate, context);
}

void EditorLayer::onImGuiRender() {
    if (!m_imgui) {
        return;
    }

    m_imgui->beginFrame();
    m_imgui->dockSpaceOverViewport();

    drawMenuBar();
    drawToolbar();

    m_hierarchy_panel->draw();
    m_inspector_panel->draw(m_hierarchy_panel->selectedEntity());

    // Viewport 面板返回当前尺寸
    const auto [width, height] = m_viewport_panel->draw();
    m_viewport_width = width;
    m_viewport_height = height;

    // 点击要在 onRender 之前发出去 —— 请求得赶上这一帧的 ID 缓冲绘制。
    // onImGuiRender 排在 onRender 之前，所以这里正好。
    if (const auto click = m_viewport_panel->consumeClick()) {
        m_renderer->requestPick(rendering::PickRequest{ click->first, click->second });
    } else if (m_auto_pick && m_viewport_width != 0 && m_frame_index % 20 == 0) {
        // 打在正中：默认场景中间那个立方体就在那儿，所以能验到「命中」而不只是「空处」。
        m_renderer->requestPick(
                rendering::PickRequest{ m_viewport_width / 2, m_viewport_height / 2 });
    }

    m_imgui->endFrame();
}

void EditorLayer::onRender() {
    if (!m_renderer || !m_scene) {
        return;
    }

    // 设置场景渲染目标尺寸（Viewport 面板的尺寸）
    m_renderer->setSceneTargetSize(m_viewport_width, m_viewport_height);

    // Viewport 面板还没量出尺寸（第一帧）时整帧跳过 —— 没有尺寸就没有 aspect。
    if (m_viewport_width == 0 || m_viewport_height == 0) {
        return;
    }

    engine::ExtractTarget target;
    target.width = m_viewport_width;
    target.height = m_viewport_height;
    const auto& render_scene = m_extractor->extract(*m_scene, *m_renderer, target);

    // 相机的两种来源，这是 EditorMode 和 PlayMode 唯一的区别：
    //
    // - EditorMode：EditorCamera 直接生成 RenderView 覆盖进去。它不是场景里的实体 ——
    //   RenderView 只是 view/projection 矩阵，不需要有 entity 撑着。
    // - PlayMode：用 extract 从场景 CameraComponent 抽出来的那个，不覆盖。
    if (m_mode == Mode::Edit) {
        m_extractor->overrideView(
                m_editor_camera->buildRenderView(m_viewport_width, m_viewport_height));
    } else if (!m_extractor->hasCamera()) {
        // PlayMode 但场景里没有 primary 相机：画不出有意义的东西，跳过这一帧。
        // 不静默回退到编辑器相机 —— 那会让「忘了放相机」看起来像正常工作。
        return;
    }

    const auto statistics = m_renderer->renderFrame(render_scene,
            m_imgui ? m_imgui->overlay() : rendering::FrameOverlay{});
    m_last_statistics = statistics;

    // 拾取结果比请求晚几帧到（读回是异步的），所以每帧问一次，不阻塞等。
    // id 0 是点在空处 —— entityForPickingId 返回空，于是取消选中。
    if (const auto pick = m_renderer->takePickResult()) {
        const auto entity = m_extractor->entityForPickingId(pick->picking_id);
        m_hierarchy_panel->setSelectedEntity(entity);
        if (m_auto_pick) {
            // 冒烟测试要能在日志里看到「打中了」，否则「一直没命中」和「路径没跑」分不开。
            core::Application::get().getLogChannel().info("Pick at ({}, {}) -> picking_id {} ({})",
                    pick->x, pick->y, pick->picking_id, entity ? "hit" : "empty");
        }
    }

    if (m_frame_index == 1 && statistics.rendered) {
        core::Application::get().getLogChannel().info("First frame rendered ({} draw calls)",
                statistics.draw_calls);
    }
}

void EditorLayer::createDefaultScene() {
    // 资产层还没做，所以网格和材质是这里现造的，句柄直接塞进组件。
    // M2 有了 AssetHandle 之后这段会换成从资产加载。
    constexpr uint32_t checker_size = 64;
    std::vector<std::byte> texels(static_cast<size_t>(checker_size) * checker_size * 4);
    for (uint32_t y = 0; y < checker_size; ++y) {
        for (uint32_t x = 0; x < checker_size; ++x) {
            const bool light = ((x / 8) + (y / 8)) % 2 == 0;
            const auto value = static_cast<std::byte>(light ? 230 : 60);
            const size_t offset = (static_cast<size_t>(y) * checker_size + x) * 4;
            texels[offset + 0] = value;
            texels[offset + 1] = value;
            texels[offset + 2] = value;
            texels[offset + 3] = static_cast<std::byte>(255);
        }
    }

    rendering::TextureDesc texture_desc;
    texture_desc.texels = std::span{ texels };
    texture_desc.width = checker_size;
    texture_desc.height = checker_size;
    texture_desc.format = rendering::TextureFormat::RGBA8Unorm;
    texture_desc.debug_name = "Editor checker";
    m_checker_texture = m_renderer->createTexture(texture_desc);

    rendering::Material material;
    material.type = rendering::MaterialType::BlinnPhong;
    material.base_color = glm::vec4{ 1.0f, 0.85f, 0.7f, 1.0f };
    material.base_color_texture = m_checker_texture;
    material.specular_color = glm::vec3{ 1.0f };
    material.specular_strength = 0.6f;
    material.shininess = 32.0f;
    m_default_material = m_renderer->createMaterial(material);
    m_cube_mesh = m_renderer->createMesh(makeCubeMesh(), "Editor cube");

    // 场景相机。EditorMode 下用不到它（走 EditorCamera），Play 时才接管。
    auto camera = m_scene->createEntity("Main Camera");
    camera.getComponent<scene::TransformComponent>().translation = glm::vec3{ 0.0f, 1.5f, 4.0f };
    camera.addComponent<engine::CameraComponent>();

    auto sun = m_scene->createEntity("Directional Light");
    sun.getComponent<scene::TransformComponent>().rotation =
            glm::quat{ glm::vec3{ glm::radians(-50.0f), glm::radians(-30.0f), 0.0f } };
    sun.addComponent<engine::DirectionalLightComponent>();

    // 三个立方体，横着排开，方便确认 Hierarchy 的选中和 Inspector 的编辑真的作用到对应实体。
    for (int index = -1; index <= 1; ++index) {
        auto cube = m_scene->createEntity("Cube");
        cube.getComponent<scene::TransformComponent>().translation =
                glm::vec3{ static_cast<float>(index) * 1.6f, 0.0f, 0.0f };
        auto& mesh_renderer = cube.addComponent<engine::MeshRendererComponent>();
        mesh_renderer.mesh = m_cube_mesh;
        mesh_renderer.material = m_default_material;
    }

    core::Application::get().getLogChannel().info("Created default scene");
}

void EditorLayer::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                // TODO
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                // TODO
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                // TODO
            }
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
                // TODO
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                core::Application::get().close();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, false)) {
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void EditorLayer::drawToolbar() {
    ImGui::Begin("Toolbar", nullptr);

    const bool playing = m_mode == Mode::Play;
    if (ImGui::Button(playing ? "Stop" : "Play", ImVec2{ 80.0f, 0.0f })) {
        if (playing) {
            exitPlayMode();
        } else {
            enterPlayMode();
        }
    }

    ImGui::SameLine();
    ImGui::TextUnformatted(playing ? "[Play]" : "[Edit]");
    ImGui::SameLine();
    ImGui::Text("| %.1f FPS | %u draws", ImGui::GetIO().Framerate, m_last_statistics.draw_calls);
    // Play 模式下场景相机是必须的，没有就整帧不画 —— 在这里说清楚，
    // 否则表现是「按了 Play 画面就黑了」而不知道为什么。
    if (playing && !m_extractor->hasCamera()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{ 1.0f, 0.4f, 0.3f, 1.0f },
                "| no primary camera — nothing to render");
    }

    ImGui::End();
}

void EditorLayer::enterPlayMode() {
    if (m_mode == Mode::Play) {
        return;
    }

    // 先存档再切模式：拷贝会抛（比如 source == destination），抛了就不该已经进了 Play。
    m_snapshot->copyEntitiesFrom(*m_scene);
    m_mode = Mode::Play;
    m_play_frame_index = 0;
    // 重新计时，否则从 Edit 模式攒下来的 accumulator 会让第一帧补一大堆 FixedUpdate。
    m_fixed_accumulator = core::FixedTimestepAccumulator{};

    core::Application::get().getLogChannel().info("Entered Play mode (scene snapshotted)");
}

void EditorLayer::exitPlayMode() {
    if (m_mode == Mode::Edit) {
        return;
    }

    // 恢复到按 Play 那一刻的样子：Play 期间新建的实体消失，被改的回退，被删的回来。
    // UUID 是拷过去的，所以 Hierarchy 里选中的实体在恢复后依然有效。
    m_scene->copyEntitiesFrom(*m_snapshot);
    m_mode = Mode::Edit;

    core::Application::get().getLogChannel().info("Returned to Edit mode (scene restored)");
}

void EditorLayer::updateEditorCamera(float deltaTime) {
    if (!m_editor_camera || !m_imgui) {
        return;
    }

    const bool mouse_owned = m_viewport_panel->isHovered();
    const bool keyboard_owned = (m_viewport_panel->isHovered() || m_viewport_panel->isFocused()) &&
                                !m_imgui->wantsTextInput();
    m_editor_camera->update(deltaTime, mouse_owned, keyboard_owned);
}

} // namespace arti::editor
