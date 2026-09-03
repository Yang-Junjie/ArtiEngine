#include "editor_layer.h"

#include "editor_camera.h"
#include "editor_context.h"
#include "editor_gizmo.h"
#include "editor_project.h"
#include "scene_document.h"

#include "panels/content_browser_panel.h"
#include "panels/hierarchy_panel.h"
#include "panels/inspector_panel.h"
#include "panels/project_settings_panel.h"
#include "panels/viewport_panel.h"

#include "platform/common/file_dialogs.h"

#include "asset/builtin_assets.h"
#include "asset/gpu_asset_cache.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "asset/prefab_asset.h"
#include "imgui/imgui_host.h"
#include "runtime/scene_renderer.h"
#include "scene/components.h"

#include "artichoco/core/application.h"
#include "artichoco/core/io/paths.h"
#include "artichoco/platform/window/sdl_vulkan_surface_source.h"
#include "artichoco/renderer/render_device.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <system_error>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <imgui.h>
#include <utility>
#include <vector>

namespace arti::editor {
namespace {

// 子资产的 source_path 是「源文件 + 后缀链」（box.gltf.mesh.0），实体名只要最前面的源文件名。
std::string sourceStemName(const std::filesystem::path& source_path) {
    std::string name = source_path.filename().string();
    if (const auto dot = name.find('.'); dot != std::string::npos) {
        name = name.substr(0, dot);
    }
    return name.empty() ? "Entity" : name;
}

// UI 字体的两段查找：exe 旁边的 resources/ 优先，没有才用构建期注入的源码树路径。
// 和 shader 同一个套路（见 shader_paths.cpp），为了让构建产物可搬移。
//
// 两边都没有时返回源码树那个路径，而不是空：ImGuiHost 加载失败会退回内建位图字体并记一条
// warn，那条 warn 里带着路径才能看出它去哪找了。
std::filesystem::path uiFontPath() {
    constexpr const char* kRelative = "fonts/Noto_Sans_SC/static/NotoSansSC-Regular.ttf";

    const auto staged = core::executableDir() / "resources" / kRelative;
    std::error_code error;
    if (std::filesystem::is_regular_file(staged, error) && !error) {
        return staged;
    }
    return std::filesystem::path{ ARTIENGINE_TOOLS_RES_DIR } / kRelative;
}

} // namespace

EditorLayer::EditorLayer() : Layer("EditorLayer") {}

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

    m_scene_renderer = std::make_unique<engine::SceneRenderer>(*m_renderer);
    m_editor_camera = std::make_unique<EditorCamera>();
    m_gizmo = std::make_unique<EditorGizmo>();

    m_project = std::make_unique<EditorProject>(*m_renderer);
    m_context = std::make_unique<EditorContext>(*m_project);
    m_document = std::make_unique<SceneDocument>(*m_context);

    engine::ImGuiHostCreateInfo imgui_info;
    imgui_info.persist_layout = true;
    imgui_info.docking = true;
    imgui_info.font_path = uiFontPath();
    m_imgui = std::make_unique<engine::ImGuiHost>(app.getWindow(), *m_renderer, imgui_info);

    m_hierarchy_panel = std::make_unique<HierarchyPanel>(*m_context);
    m_inspector_panel = std::make_unique<InspectorPanel>(*m_context);
    m_viewport_panel = std::make_unique<ViewportPanel>(*m_renderer);
    m_content_browser_panel = std::make_unique<ContentBrowserPanel>(*m_project);
    m_project_settings_panel = std::make_unique<ProjectSettingsPanel>(*m_document);

    const auto output = m_renderer->outputInfo();
    app.getLogChannel().info("Scene Editor ready, output {}x{}", output.width, output.height);
}

void EditorLayer::onDetach() {
    if (m_renderer) {
        m_renderer->waitIdle();
    }
    // 设置对话框引用 document，先于它析构。
    m_project_settings_panel.reset();
    m_viewport_panel.reset();
    m_inspector_panel.reset();
    m_hierarchy_panel.reset();
    m_content_browser_panel.reset();
    m_imgui.reset();
    m_gizmo.reset();
    m_editor_camera.reset();
    m_scene_renderer.reset();
    // 顺序：document 引用 context，context 引用 project。
    m_document.reset();
    m_context.reset();
    m_project.reset();
    m_renderer.reset();
    m_render_device.reset();
}

void EditorLayer::onUpdate(core::Timestep deltaTime) {
    ++m_frame_index;
    if (!m_context) {
        return;
    }

    const float dt = deltaTime.getSeconds();
    // 两条轴各判一次，**别写回 if-else**：Simulate 下系统在跑，而相机还是编辑器的，两件事同时
    // 成立（D8）。Play 下相机交给场景的 primary，编辑器相机不该跟着动。
    if (m_context->isSimulating()) {
        m_context->updateSimulation(dt);
    }
    if (!m_context->isGameView()) {
        updateEditorCamera(dt);
    }
}

void EditorLayer::onImGuiRender() {
    if (!m_imgui) {
        return;
    }

    m_imgui->beginFrame();
    // 必须紧跟 ImGui::NewFrame()。漏掉手柄完全不响应且不报错。
    ImGuizmo::BeginFrame();

    m_imgui->dockSpaceOverViewport();

    const bool gizmo_enabled = !m_context->isGameView();
    m_gizmo->handleShortcuts(gizmo_enabled);

    drawMenuBar();
    drawToolbar();

    m_hierarchy_panel->draw();
    m_inspector_panel->draw();
    m_content_browser_panel->draw();
    // 模态：菜单项只置个标记，真正的 OpenPopup 在这里，和 BeginPopupModal 同一层 ID 栈。
    m_project_settings_panel->draw();

    const auto selected = m_context->selectedEntity();
    const auto [width, height] = m_viewport_panel->draw([&](const ViewportPanel::ImageRect& rect) {
        if (!gizmo_enabled) {
            return;
        }
        m_gizmo->draw(m_context->scene(), selected, m_scene_renderer->renderScene().view,
                rect.x, rect.y, rect.width, rect.height);
    });
    handleViewportAssetDrop(m_viewport_panel->imageRect().x, m_viewport_panel->imageRect().y,
            m_viewport_panel->imageRect().width, m_viewport_panel->imageRect().height);
    m_viewport_width = width;
    m_viewport_height = height;

    if (const auto click = m_viewport_panel->consumeClick()) {
        m_renderer->requestPick(rendering::PickRequest{ click->first, click->second });
    }

    m_imgui->endFrame();
}

void EditorLayer::onRender() {
    // SceneRenderer::submit() 是**唯一**提交 ImGui draw data 的地方（onImGuiRender 只生成，
    // 不提交）。所以这个函数必须无条件走到它 —— 任何 early return 都是整个界面黑屏，
    // 而黑屏时用户连菜单都点不到，没法自救。
    //
    // 下面这道守卫是安全的：这三个都是 onAttach 里和 m_imgui 一起建起来的，
    // 它们不在的时候也没有 UI 可丢。
    if (!m_renderer || !m_scene_renderer || !m_context) {
        return;
    }

    engine::SceneRenderer::ViewportInfo viewport;
    viewport.width = m_viewport_width;
    viewport.height = m_viewport_height;
    // Edit 和 Simulate 下相机是编辑器的，不是场景里的 —— 盖掉场景里的 primary 相机。
    // 尺寸为 0 时不算：宽高比会变成 0/0。
    if (!m_context->isGameView() && m_viewport_width != 0 && m_viewport_height != 0) {
        viewport.view_override =
                m_editor_camera->buildRenderView(m_viewport_width, m_viewport_height);
    }

    // 没开项目就不给资产。场景画不出来的几种情况（没开项目、面板尺寸为 0、Play 模式没相机）
    // 由 SceneRenderer 统一处理成提交一个空场景：没有 draw、只有 UI 覆盖层。
    // IntoUI 模式下 ImGuiPass 会清 backbuffer，所以界面正常。
    engine::asset::GPUAssetCache* assets =
            m_context->isProjectOpen() ? &m_project->gpuAssets() : nullptr;
    const bool has_scene = m_scene_renderer->prepare(m_context->scene(), assets, viewport);

    // 调试线必须在 submit **之前**提交：它们只作用于紧接着的那一帧，和 requestPick 一样。
    // 放在 prepare 之后是因为要用相机（编辑器相机的覆盖也已经生效了）。
    if (has_scene && !m_context->isGameView()) {
        submitSelectionGizmos();
    }

    const auto statistics =
            m_scene_renderer->submit(m_imgui ? m_imgui->overlay() : rendering::FrameOverlay{});
    m_last_statistics = statistics;

    if (const auto pick = m_renderer->takePickResult()) {
        const auto entity = m_scene_renderer->entityForPickingId(pick->picking_id);
        m_context->setSelectedEntity(entity);
    }

    if (m_frame_index == 1 && statistics.rendered) {
        core::Application::get().getLogChannel().info("First frame rendered ({} draw calls)",
                statistics.draw_calls);
    }
}

void EditorLayer::newProject() {
    // 选目录而不是选文件：新项目的 .artiproj 还不存在，要先定根目录。
    const auto root = FileDialogs::selectDirectory({});
    if (root.empty()) {
        return;
    }

    if (!m_project->create(root, root.filename().string())) {
        return;
    }
    // 换项目后旧场景引用的资产 UUID 在新 catalog 里可能不存在，所以重建场景。
    // createNew() 里的 reset() 也会清掉当前文件名 —— 否则 Ctrl+S 会把新项目的场景
    // 写回上一个项目里去。
    m_document->createNew();
}

void EditorLayer::openProject() {
    const auto file = FileDialogs::openFile("Arti Project\0*.artiproj\0", {});
    if (file.empty()) {
        return;
    }

    if (!m_project->open(file)) {
        return;
    }
    // 项目记着上次打开的场景就接着上次的，否则给个默认场景。
    if (!m_document->loadLastOpen()) {
        m_document->createNew();
    }
}

void EditorLayer::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...")) {
                newProject();
            }
            if (ImGui::MenuItem("Open Project...")) {
                openProject();
            }
            ImGui::Separator();
            const bool project_open = m_context->isProjectOpen();
            if (ImGui::MenuItem("New Scene", "Ctrl+N", false, project_open)) {
                m_document->createNew();
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, project_open)) {
                m_document->open();
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, project_open)) {
                m_document->save();
            }
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S", false, project_open)) {
                m_document->saveAs();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Project Settings...", nullptr, false, project_open)) {
                m_project_settings_panel->open();
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

    // 两个模式按钮：当前就是这个模式时它变成 Stop，另一个模式正在跑时它禁用 ——
    // D8 不做 Simulate ↔ Play 的直接切换，所以「两个都能按」这种状态不该出现。
    const auto mode = m_context->mode();
    const auto modeButton = [this, mode](const char* label, EditorContext::Mode target) {
        const bool active = mode == target;
        // 按钮的 ID 就是它的文字，而文字会在 Stop / Play 之间变 —— 显式 PushID 钉住身份，
        // 免得将来两个按钮在某个状态下文字撞车。
        ImGui::PushID(label);
        ImGui::BeginDisabled(!active && mode != EditorContext::Mode::Edit);
        if (ImGui::Button(active ? "Stop" : label, ImVec2{ 80.0f, 0.0f })) {
            if (active) {
                m_context->exitToEdit();
            } else {
                m_context->enterMode(target);
            }
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    };

    modeButton("Play", EditorContext::Mode::Play);
    ImGui::SameLine();
    modeButton("Simulate", EditorContext::Mode::Simulate);

    ImGui::SameLine();
    ImGui::TextUnformatted(mode == EditorContext::Mode::Play         ? "[Play]"
                    : mode == EditorContext::Mode::Simulate ? "[Simulate]"
                                                            : "[Edit]");

    // gizmo 的这几个控件跟着 gizmo 走，也就是「是不是游戏视角」那条轴 —— Simulate 下 gizmo
    // 还在，所以它们也还在。
    if (!m_context->isGameView()) {
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
    // draws 和 culled 并排显示：两个数加起来应该等于场景里的 PBR submesh 总数，
    // 转相机的时候盯着这个和是不是不变，就能一眼看出剔除有没有算漏或算重。
    ImGui::Text("| %.1f FPS | %u draws | %u culled | %u shadow culled", ImGui::GetIO().Framerate,
            m_last_statistics.draw_calls, m_last_statistics.culled, m_last_statistics.shadow_culled);
    if (m_context->isGameView() && !m_scene_renderer->hasCamera()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{ 1.0f, 0.4f, 0.3f, 1.0f },
                "| no primary camera — nothing to render");
    }

    ImGui::End();
}

void EditorLayer::handleViewportAssetDrop(float rect_x, float rect_y, float rect_width,
        float rect_height) {
    const ImVec2 mouse = ImGui::GetMousePos();
    if (mouse.x < rect_x || mouse.x >= rect_x + rect_width || mouse.y < rect_y ||
            mouse.y >= rect_y + rect_height) {
        return;
    }
    if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        return;
    }
    const auto* payload = ImGui::GetDragDropPayload();
    if (payload == nullptr || !payload->IsDataType(ContentBrowserPanel::kAssetPayloadType) ||
            payload->DataSize != sizeof(core::UUID::Value)) {
        return;
    }
    core::UUID::Value value = 0;
    std::memcpy(&value, payload->Data, sizeof(value));
    spawnAssetEntity(core::UUID{ value });
}

void EditorLayer::spawnAssetEntity(core::UUID asset) {
    auto& log = core::Application::get().getLogChannel();
    if (!m_context || !m_context->isProjectOpen()) {
        return;
    }
    const auto metadata = m_project->assets().catalog().find(asset);
    if (!metadata) {
        log.error("Cannot spawn asset {}: not in the catalog", asset.toString());
        return;
    }

    auto& scene = m_context->scene();

    if (metadata->type == std::string{ engine::asset::kMeshAssetType }) {
        auto entity = scene.createEntity(sourceStemName(metadata->source_path));
        auto& mesh_renderer = entity.addComponent<engine::MeshRendererComponent>();
        mesh_renderer.mesh = arti::asset::AssetHandle<engine::asset::MeshAsset>{ asset };
        mesh_renderer.materials.push_back(
                arti::asset::AssetHandle<engine::asset::MaterialAsset>{
                        engine::asset::kBuiltinDefaultMaterial });
        m_context->setSelectedEntity(entity.getComponent<scene::IDComponent>().id);
        m_document->markDirty();
        log.info("Spawned '{}' into the scene", metadata->source_path.string());
        return;
    }

    if (metadata->type == std::string{ engine::asset::kPrefabAssetType }) {
        const auto prefab = m_project->assets().load<engine::asset::PrefabAsset>(asset);
        if (!prefab) {
            log.error("Failed to load the prefab {}", asset.toString());
            return;
        }

        const auto& nodes = prefab->nodes();
        std::vector<scene::Entity> created;
        created.reserve(nodes.size());
        for (const auto& node: nodes) {
            auto entity = scene.createEntity(node.name.empty() ? "Prefab Node" : node.name);
            auto& transform = entity.getComponent<scene::TransformComponent>();
            glm::vec3 translation{ 0.0f };
            glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
            glm::vec3 scale{ 1.0f };
            glm::vec3 skew{ 0.0f };
            glm::vec4 perspective{ 1.0f };
            if (glm::decompose(node.local_transform, scale, rotation, translation, skew,
                        perspective)) {
                transform.translation = translation;
                transform.rotation = rotation;
                transform.scale = scale;
            }
            if (node.mesh.isValid()) {
                auto& mesh_renderer = entity.addComponent<engine::MeshRendererComponent>();
                mesh_renderer.mesh =
                        arti::asset::AssetHandle<engine::asset::MeshAsset>{ node.mesh };
                for (const auto material: node.materials) {
                    mesh_renderer.materials.push_back(
                            arti::asset::AssetHandle<engine::asset::MaterialAsset>{ material });
                }
                if (mesh_renderer.materials.empty()) {
                    mesh_renderer.materials.push_back(
                            arti::asset::AssetHandle<engine::asset::MaterialAsset>{
                                    engine::asset::kBuiltinDefaultMaterial });
                }
            }
            created.push_back(entity);
        }
        for (size_t index = 0; index < created.size(); ++index) {
            const uint32_t parent = nodes[index].parent;
            if (parent != engine::asset::kNoParentNode && parent < created.size()) {
                scene.setParent(created[index], created[parent]);
            }
        }
        if (!created.empty()) {
            m_context->setSelectedEntity(created.front().getComponent<scene::IDComponent>().id);
        }
        m_document->markDirty();
        log.info("Spawned prefab '{}' ({} node(s))", metadata->source_path.string(),
                created.size());
        return;
    }

    log.warn("Asset {} is of type '{}' and cannot be spawned directly",
            metadata->source_path.string(), metadata->type);
}

// 选中实体的调试轮廓。规则只有一条：选中的实体，按它挂了什么组件画对应的线。
//
// 只在编辑模式画，不在 Play 模式画 —— Play 是「看成品」，选择框会碍事。
void EditorLayer::submitSelectionGizmos() {
    // 颜色都是显示线性的，写进去就是看到的颜色（见 rendering::DebugLine）。
    constexpr glm::vec4 kBoundsColor{ 1.0f, 0.6f, 0.1f, 1.0f };
    constexpr glm::vec4 kLightColor{ 1.0f, 0.9f, 0.3f, 1.0f };

    const auto& selected = m_context->selectedEntity();
    if (!selected) {
        return;
    }
    auto entity = m_context->scene().findEntity(*selected);
    if (!entity.isValid()) {
        return;
    }
    if (!entity.hasComponent<scene::WorldTransformComponent>()) {
        return;
    }
    // WorldTransformComponent 是场景自己维护的，只有 const 访问 —— 这里也只读。
    const glm::mat4& world = entity.getComponent<scene::WorldTransformComponent>().world;
    const glm::vec3 position{ world[3] };

    // 网格：局部包围盒变换到世界。局部盒子从 Renderer 查 —— 顶点数据上传完就不在 CPU 侧了。
    if (entity.hasComponent<engine::MeshRendererComponent>()) {
        const auto& mesh_renderer = entity.getComponent<engine::MeshRendererComponent>();
        const auto handle = m_project->gpuAssets().meshHandle(mesh_renderer.mesh.id());
        if (const auto info = m_renderer->meshInfo(handle)) {
            m_renderer->drawAABB(info->bounds.transformed(world), kBoundsColor);
        }
    }

    // 点光源：range 就是那个线框球的半径。没有它的话点光源在视口里完全看不见。
    if (entity.hasComponent<engine::PointLightComponent>()) {
        const auto& light = entity.getComponent<engine::PointLightComponent>();
        m_renderer->drawWireSphere(position, light.range, kLightColor);
    }

    // 聚光灯：四条母线 + 远端一个环，够看出朝向和张角。
    if (entity.hasComponent<engine::SpotLightComponent>()) {
        const auto& light = entity.getComponent<engine::SpotLightComponent>();
        const glm::vec3 forward = glm::normalize(glm::vec3{ -world[2] });
        const glm::vec3 right = glm::normalize(glm::vec3{ world[0] });
        const glm::vec3 up = glm::normalize(glm::vec3{ world[1] });
        const glm::vec3 end = position + forward * light.range;
        const float outer = glm::radians(light.outer_cone_degrees);
        const float ring_radius = std::tan(outer) * light.range;

        constexpr uint32_t kRingSegments = 24;
        constexpr float kTwoPi = 6.28318530718f;
        glm::vec3 previous{};
        for (uint32_t index = 0; index <= kRingSegments; ++index) {
            const float angle = static_cast<float>(index) * kTwoPi / kRingSegments;
            const glm::vec3 point =
                    end + (right * std::cos(angle) + up * std::sin(angle)) * ring_radius;
            if (index > 0) {
                m_renderer->drawLine(previous, point, kLightColor);
            }
            // 四条母线，每隔四分之一圈拉一条。
            if (index % (kRingSegments / 4) == 0 && index < kRingSegments) {
                m_renderer->drawLine(position, point, kLightColor);
            }
            previous = point;
        }
    }

    // 方向光没有位置也没有范围，只画一条朝向线 —— 至少能看出它转到哪儿了。
    if (entity.hasComponent<engine::DirectionalLightComponent>()) {
        const glm::vec3 forward = glm::normalize(glm::vec3{ -world[2] });
        m_renderer->drawLine(position, position + forward * 2.0f, kLightColor);
    }
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
