#include "editor_layer.h"

#include "content_browser_panel.h"
#include "editor_camera.h"
#include "editor_gizmo.h"
#include "editor_project.h"
#include "file_dialogs.h"
#include "hierarchy_panel.h"
#include "inspector_panel.h"
#include "viewport_panel.h"

#include "scene/component_registration.h"
#include "scene/component_serialization.h"
#include "scene/components.h"
#include "scene/render_scene_extractor.h"
#include "asset/builtin_assets.h"
#include "asset/gpu_asset_cache.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "asset/prefab_asset.h"
#include "asset/texture_asset.h"
#include "imgui/imgui_host.h"

#include "artichoco/core/application.h"
#include "artichoco/platform/window/sdl_vulkan_surface_source.h"
#include "artichoco/project/project_manager.h"
#include "artichoco/renderer/render_device.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"
#include "artichoco/scene/scene_serializer.h"

#include <cstddef>
#include <cstring>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <imgui.h>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace arti::editor {
namespace {

// 子资产的 source_path 是「源文件 + 后缀链」（box.obj.mesh.0），实体名只要最前面的源文件名。
std::string sourceStemName(const std::filesystem::path& source_path) {
    std::string name = source_path.filename().string();
    if (const auto dot = name.find('.'); dot != std::string::npos) {
        name = name.substr(0, dot);
    }
    return name.empty() ? "Entity" : name;
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

    // 两张表一起注册：拷贝表给 Play/Stop 的快照用，序列化表给存读用。
    // 漏任何一张都是静默丢组件，所以是同一个入口。
    m_serialization = std::make_unique<scene::SceneSerializationRegistry>();
    engine::registerSceneComponents(m_serialization.get());
    m_serializer = std::make_unique<scene::SceneSerializer>(*m_serialization);

    m_scene = std::make_unique<scene::Scene>();
    m_snapshot = std::make_unique<scene::Scene>();
    m_extractor = std::make_unique<engine::RenderSceneExtractor>();
    m_editor_camera = std::make_unique<EditorCamera>();
    m_gizmo = std::make_unique<EditorGizmo>();
    m_project = std::make_unique<EditorProject>(*m_renderer);

    engine::ImGuiHostCreateInfo imgui_info;
    imgui_info.persist_layout = true;
    imgui_info.docking = true;
    m_imgui = std::make_unique<engine::ImGuiHost>(app.getWindow(), *m_renderer, imgui_info);

    m_hierarchy_panel = std::make_unique<HierarchyPanel>(*m_scene);
    m_inspector_panel = std::make_unique<InspectorPanel>(*m_scene);
    m_viewport_panel = std::make_unique<ViewportPanel>(*m_renderer);
    m_content_browser_panel = std::make_unique<ContentBrowserPanel>(*m_project);

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
    m_content_browser_panel.reset();
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
    m_content_browser_panel->draw();

    const auto selected = m_hierarchy_panel->selectedEntity();
    const auto [width, height] = m_viewport_panel->draw([&](const ViewportPanel::ImageRect& rect) {
        if (!gizmo_enabled) {
            return;
        }
        m_gizmo->draw(*m_scene, selected, m_extractor->renderScene().view, rect.x, rect.y,
                rect.width, rect.height);
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
    if (!m_renderer) {
        return;
    }

    // renderFrame 是**唯一**提交 ImGui draw data 的地方（onImGuiRender 只生成，不提交）。
    // 所以这个函数必须无条件走到它 —— 任何 early return 都是整个界面黑屏，
    // 而黑屏时用户连菜单都点不到，没法自救。
    //
    // 场景画不出来的情况（没开项目、面板尺寸为 0、Play 模式没相机）就提交一个空场景：
    // 没有 draw、只有 UI 覆盖层。IntoUI 模式下 ImGuiPass 会清 backbuffer，所以界面正常。
    m_renderer->setSceneTargetSize(m_viewport_width, m_viewport_height);

    const bool can_extract = m_scene && m_project && m_project->isOpen() && m_viewport_width != 0 &&
                             m_viewport_height != 0;

    const rendering::RenderScene* submit = nullptr;
    rendering::RenderScene empty;

    if (can_extract) {
        engine::ExtractTarget target;
        target.width = m_viewport_width;
        target.height = m_viewport_height;
        submit = &m_extractor->extract(*m_scene, m_project->gpuAssets(), *m_renderer, target);

        if (m_mode == Mode::Edit) {
            m_extractor->overrideView(
                    m_editor_camera->buildRenderView(m_viewport_width, m_viewport_height));
        } else if (!m_extractor->hasCamera()) {
            // Play 模式但场景里没有 primary 相机：不画场景，但 UI 要留着 ——
            // 否则用户按不到 Stop。toolbar 上那行红字说明原因。
            submit = &empty;
        }
    } else {
        submit = &empty;
    }

    const auto statistics = m_renderer->renderFrame(*submit,
            m_imgui ? m_imgui->overlay() : rendering::FrameOverlay{});
    m_last_statistics = statistics;

    if (const auto pick = m_renderer->takePickResult()) {
        const auto entity = m_extractor->entityForPickingId(pick->picking_id);
        m_hierarchy_panel->setSelectedEntity(entity);
    }

    if (m_frame_index == 1 && statistics.rendered) {
        core::Application::get().getLogChannel().info("First frame rendered ({} draw calls)",
                statistics.draw_calls);
    }
}

void EditorLayer::resetSceneState() {
    // Play 状态下换场景会让快照指向已经不存在的实体，所以先退回 Edit。
    if (m_mode == Mode::Play) {
        exitPlayMode();
    }
    m_scene->clearEntities();
    m_hierarchy_panel->setSelectedEntity(std::nullopt);
    m_scene_dirty = false;
}

void EditorLayer::newScene() {
    resetSceneState();
    m_scene_file.clear();
    createDefaultScene();
    core::Application::get().getLogChannel().info("New scene");
}

void EditorLayer::openScene() {
    // 起始目录用项目根，这样对话框直接落在项目里而不是上次的随机位置。
    const auto root = m_project->rootPath();
    const auto file = FileDialogs::openFile("Arti Scene\0*.artiscene\0",
            root ? root->string() : std::string{});
    if (file.empty()) {
        return;
    }

    resetSceneState();
    try {
        m_serializer->load(file, *m_scene);
        m_scene_file = file;
    } catch (const std::exception& exception) {
        // 读失败不该让编辑器退出，但场景已经被 clearEntities 清了 —— 所以给个空场景，
        // 而不是留下半个读进来的场景假装成功。
        core::Application::get().getLogChannel().error("Failed to open scene: {}",
                exception.what());
        m_scene->clearEntities();
        m_scene_file.clear();
    }
}

bool EditorLayer::saveScene(const std::filesystem::path& path) {
    if (path.empty()) {
        saveSceneAs();
        return !m_scene_file.empty();
    }

    try {
        m_serializer->save(*m_scene, path);
    } catch (const std::exception& exception) {
        core::Application::get().getLogChannel().error("Failed to save scene: {}",
                exception.what());
        return false;
    }

    m_scene_file = path;
    m_scene_dirty = false;

    // 记进项目，下次打开能回到这个场景。ProjectInfo 里那两个字段是相对项目根的。
    auto& projects = project::ProjectManager::instance();
    if (const auto& info = projects.getProjectInfo(); info) {
        if (const auto root = projects.getProjectRootPath()) {
            std::error_code error;
            const auto relative = std::filesystem::relative(path, *root, error);
            // 场景存在项目外面时 relative 会带 ..，那种路径 ProjectManager 会拒，所以只在
            // 确实位于项目内时才记。
            if (!error && !relative.empty() &&
                    relative.native().find(L"..") == std::wstring::npos) {
                project::ProjectInfo updated = *info;
                updated.last_open_scene = relative;
                projects.setProjectInfo(updated);
                projects.saveProject();
            }
        }
    }
    return true;
}

void EditorLayer::saveSceneAs() {
    const auto root = m_project->rootPath();
    const auto file = FileDialogs::saveFile("Arti Scene\0*.artiscene\0",
            root ? (*root / "Untitled.artiscene").string() : std::string{});
    if (file.empty()) {
        return;
    }

    auto with_extension = file;
    if (!with_extension.has_extension()) {
        with_extension.replace_extension(".artiscene");
    }
    saveScene(with_extension);
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
    // m_scene_file 也要清 —— 否则 Ctrl+S 会把新项目的场景写回上一个项目里去。
    resetSceneState();
    m_scene_file.clear();
    createDefaultScene();
}

void EditorLayer::openProject() {
    const auto file = FileDialogs::openFile("Arti Project\0*.artiproj\0", {});
    if (file.empty()) {
        return;
    }

    if (!m_project->open(file)) {
        return;
    }
    resetSceneState();
    m_scene_file.clear();
    // 项目记着上次打开的场景就接着上次的，否则给个默认场景。
    if (!loadLastOpenScene()) {
        createDefaultScene();
    }
}

bool EditorLayer::loadLastOpenScene() {
    auto& log = core::Application::get().getLogChannel();
    auto& projects = project::ProjectManager::instance();

    const auto& info = projects.getProjectInfo();
    if (!info || info->last_open_scene.empty()) {
        return false;
    }
    // last_open_scene 是相对项目根的（saveScene 那边就是这么写进去的）。
    const auto root = projects.getProjectRootPath();
    if (!root) {
        return false;
    }

    const auto file = *root / info->last_open_scene;
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error) || error) {
        // 场景被删了或者项目是从别的机器拷过来的 —— 不是错误，只是没得可恢复。
        log.info("Last open scene '{}' does not exist, falling back to a default scene",
                info->last_open_scene.string());
        return false;
    }

    try {
        m_serializer->load(file, *m_scene);
    } catch (const std::exception& exception) {
        // 和 openScene 一样：读失败不能留下半个场景。清掉，让调用方摆默认场景。
        log.error("Failed to load the last open scene '{}': {}", file.string(), exception.what());
        m_scene->clearEntities();
        return false;
    }

    m_scene_file = file;
    m_scene_dirty = false;
    log.info("Restored the last open scene '{}'", info->last_open_scene.string());
    return true;
}

void EditorLayer::createDefaultScene() {
    // 引用内置资产，不再现场造网格和材质。内置资产的 UUID 是常量，所以这个场景存盘之后
    // 在任何项目里读回来都指向同一个立方体。
    const arti::asset::AssetHandle<engine::asset::MeshAsset> cube_mesh{
        engine::asset::kBuiltinCubeMesh
    };
    const arti::asset::AssetHandle<engine::asset::MaterialAsset> pbr_material{
        engine::asset::kBuiltinDefaultMaterial
    };

    auto camera = m_scene->createEntity("Main Camera");
    camera.getComponent<scene::TransformComponent>().translation = glm::vec3{ 0.0f, 1.5f, 4.0f };
    camera.addComponent<engine::CameraComponent>();

    auto sun = m_scene->createEntity("Directional Light");
    sun.getComponent<scene::TransformComponent>().rotation =
            glm::quat{ glm::vec3{ glm::radians(-50.0f), glm::radians(-30.0f), 0.0f } };
    sun.addComponent<engine::DirectionalLightComponent>();

    // 环境光做成显式实体，和相机、光源一致 —— 否则这个组件在 Add Component 菜单里躺着，
    // 没人知道它存在。默认值等于引入 EnvironmentDesc 之前 pass 里硬编码的那个环境光，
    // 所以摆上去画面不变。
    auto environment = m_scene->createEntity("Environment");
    environment.addComponent<engine::EnvironmentComponent>();

    // 中间那个用 PBR 材质，左右两个用默认的 Blinn-Phong。并排放是为了能直接比出两条 pass
    // 的差别。
    for (int index = -1; index <= 1; ++index) {
        auto cube = m_scene->createEntity("Cube");
        cube.getComponent<scene::TransformComponent>().translation =
                glm::vec3{ static_cast<float>(index) * 1.6f, 0.0f, 0.0f };
        auto& mesh_renderer = cube.addComponent<engine::MeshRendererComponent>();
        mesh_renderer.mesh = cube_mesh;
        mesh_renderer.materials.push_back(pbr_material);
    }

    core::Application::get().getLogChannel().info("Created default scene");
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
            // 场景操作需要项目：场景里的资产 UUID 要在项目的 catalog 里查得到，
            // 场景路径也要记进 ProjectInfo。没项目时置灰。
            const bool project_open = m_project && m_project->isOpen();
            if (ImGui::MenuItem("New Scene", "Ctrl+N", false, project_open)) {
                newScene();
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, project_open)) {
                openScene();
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, project_open)) {
                saveScene(m_scene_file);
            }
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S", false, project_open)) {
                saveSceneAs();
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
    if (!m_scene || !m_project || !m_project->isOpen()) {
        return;
    }
    const auto metadata = m_project->assets().catalog().find(asset);
    if (!metadata) {
        log.error("Cannot spawn asset {}: not in the catalog", asset.toString());
        return;
    }

    if (metadata->type == std::string{ engine::asset::kMeshAssetType }) {
        auto entity = m_scene->createEntity(sourceStemName(metadata->source_path));
        auto& mesh_renderer = entity.addComponent<engine::MeshRendererComponent>();
        mesh_renderer.mesh = arti::asset::AssetHandle<engine::asset::MeshAsset>{ asset };
        mesh_renderer.materials.push_back(
                arti::asset::AssetHandle<engine::asset::MaterialAsset>{
                        engine::asset::kBuiltinDefaultMaterial });
        m_hierarchy_panel->setSelectedEntity(
                entity.getComponent<scene::IDComponent>().id);
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
            auto entity = m_scene->createEntity(node.name.empty() ? "Prefab Node" : node.name);
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
                m_scene->setParent(created[index], created[parent]);
            }
        }
        if (!created.empty()) {
            m_hierarchy_panel->setSelectedEntity(
                    created.front().getComponent<scene::IDComponent>().id);
        }
        log.info("Spawned prefab '{}' ({} node(s))", metadata->source_path.string(),
                created.size());
        return;
    }

    log.warn("Asset {} is of type '{}' and cannot be spawned directly",
            metadata->source_path.string(), metadata->type);
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
