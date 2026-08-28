#include "editor_layer.h"

#include "editor_camera.h"
#include "editor_gizmo.h"
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

    rendering::RendererCreateInfo renderer_info;
    renderer_info.present = rendering::PresentMode::IntoUI;
    m_renderer = std::make_unique<rendering::Renderer>(*m_render_device, renderer_info);

    engine::registerSceneComponents();

    m_scene = std::make_unique<scene::Scene>();
    m_snapshot = std::make_unique<scene::Scene>();
    m_extractor = std::make_unique<engine::RenderSceneExtractor>();
    m_editor_camera = std::make_unique<EditorCamera>();
    m_gizmo = std::make_unique<EditorGizmo>();

    engine::ImGuiHostCreateInfo imgui_info;
    imgui_info.persist_layout = true;
    imgui_info.docking = true;
    m_imgui = std::make_unique<engine::ImGuiHost>(app.getWindow(), *m_renderer, imgui_info);

    m_hierarchy_panel = std::make_unique<HierarchyPanel>(*m_scene);
    m_inspector_panel = std::make_unique<InspectorPanel>(*m_scene);
    m_viewport_panel = std::make_unique<ViewportPanel>(*m_renderer);

    if (!m_scene_path.empty()) {
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
    m_gizmo.reset();
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
        updateEditorCamera(dt);
        return;
    }

    scene::UpdateContext context;
    context.deltaTime = dt;
    context.fixedDeltaTime = m_fixed_accumulator.fixedDeltaTime();
    context.frameIndex = m_play_frame_index++;

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
    // 必须紧跟 ImGui::NewFrame()。漏掉手柄完全不响应且不报错。
    ImGuizmo::BeginFrame();

    m_imgui->dockSpaceOverViewport();

    const bool gizmo_enabled = m_mode == Mode::Edit;
    m_gizmo->handleShortcuts(gizmo_enabled);

    drawMenuBar();
    drawToolbar();

    m_hierarchy_panel->draw();
    m_inspector_panel->draw(m_hierarchy_panel->selectedEntity());

    const auto selected = m_hierarchy_panel->selectedEntity();
    const auto [width, height] = m_viewport_panel->draw([&](const ViewportPanel::ImageRect& rect) {
        if (!gizmo_enabled) {
            return;
        }
        m_gizmo->draw(*m_scene, selected, m_extractor->renderScene().view, rect.x, rect.y,
                rect.width, rect.height);
    });
    m_viewport_width = width;
    m_viewport_height = height;

    if (const auto click = m_viewport_panel->consumeClick()) {
        m_renderer->requestPick(rendering::PickRequest{ click->first, click->second });
    } else if (m_auto_pick && m_viewport_width != 0 && m_frame_index % 20 == 0) {
        m_renderer->requestPick(
                rendering::PickRequest{ m_viewport_width / 2, m_viewport_height / 2 });
    }

    m_imgui->endFrame();
}

void EditorLayer::onRender() {
    if (!m_renderer || !m_scene) {
        return;
    }

    m_renderer->setSceneTargetSize(m_viewport_width, m_viewport_height);

    if (m_viewport_width == 0 || m_viewport_height == 0) {
        return;
    }

    engine::ExtractTarget target;
    target.width = m_viewport_width;
    target.height = m_viewport_height;
    const auto& render_scene = m_extractor->extract(*m_scene, *m_renderer, target);

    if (m_mode == Mode::Edit) {
        m_extractor->overrideView(
                m_editor_camera->buildRenderView(m_viewport_width, m_viewport_height));
    } else if (!m_extractor->hasCamera()) {
        return;
    }

    const auto statistics = m_renderer->renderFrame(render_scene,
            m_imgui ? m_imgui->overlay() : rendering::FrameOverlay{});
    m_last_statistics = statistics;

    if (const auto pick = m_renderer->takePickResult()) {
        const auto entity = m_extractor->entityForPickingId(pick->picking_id);
        m_hierarchy_panel->setSelectedEntity(entity);
        if (m_auto_pick) {
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

    auto camera = m_scene->createEntity("Main Camera");
    camera.getComponent<scene::TransformComponent>().translation = glm::vec3{ 0.0f, 1.5f, 4.0f };
    camera.addComponent<engine::CameraComponent>();

    auto sun = m_scene->createEntity("Directional Light");
    sun.getComponent<scene::TransformComponent>().rotation =
            glm::quat{ glm::vec3{ glm::radians(-50.0f), glm::radians(-30.0f), 0.0f } };
    sun.addComponent<engine::DirectionalLightComponent>();

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
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
            }
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
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

    if (!playing) {
        ImGui::SameLine();
        const auto operation = m_gizmo->operation();
        const char* label = operation == ImGuizmo::TRANSLATE ? "Translate"
                            : operation == ImGuizmo::ROTATE  ? "Rotate"
                                                             : "Scale";
        ImGui::Text("| %s (Alt+Q/W/E)", label);

        ImGui::SameLine();
        bool world = m_gizmo->mode() == ImGuizmo::WORLD;
        if (ImGui::Checkbox("World", &world)) {
            m_gizmo->setMode(world ? ImGuizmo::WORLD : ImGuizmo::LOCAL);
        }
    }

    ImGui::SameLine();
    ImGui::Text("| %.1f FPS | %u draws", ImGui::GetIO().Framerate, m_last_statistics.draw_calls);
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

    m_snapshot->copyEntitiesFrom(*m_scene);
    m_mode = Mode::Play;
    m_play_frame_index = 0;
    m_fixed_accumulator = core::FixedTimestepAccumulator{};

    core::Application::get().getLogChannel().info("Entered Play mode (scene snapshotted)");
}

void EditorLayer::exitPlayMode() {
    if (m_mode == Mode::Edit) {
        return;
    }

    m_scene->copyEntitiesFrom(*m_snapshot);
    m_mode = Mode::Edit;

    core::Application::get().getLogChannel().info("Returned to Edit mode (scene restored)");
}

void EditorLayer::updateEditorCamera(float deltaTime) {
    if (!m_editor_camera || !m_imgui) {
        return;
    }

    const bool mouse_owned = m_viewport_panel->isHovered() && !m_gizmo->isUsing();

    // 用 wantsTextInput 而非 wantsKeyboardInput：后者在开了 NavEnableKeyboard 后恒为 true。
    const bool keyboard_owned = !m_imgui->wantsTextInput();

    m_editor_camera->update(deltaTime, mouse_owned, keyboard_owned);
}

} // namespace arti::editor
