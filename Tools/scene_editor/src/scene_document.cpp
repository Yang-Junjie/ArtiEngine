#include "scene_document.h"

#include "editor_context.h"
#include "editor_paths.h"
#include "editor_project.h"

#include "platform/common/file_dialogs.h"

#include "asset/builtin_assets.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "scene/components.h"

#include "artichoco/core/application.h"
#include "artichoco/project/project_manager.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <glm/gtc/quaternion.hpp>

#include <string>
#include <system_error>

namespace arti::editor {
namespace {

const core::Logger::Channel& log() { return core::Application::get().getLogChannel(); }

constexpr const char* kSceneFilter = "Arti Scene\0*.artiscene\0";
constexpr const char* kSceneExtension = ".artiscene";

std::string projectRoot(EditorContext& context) {
    const auto root = context.project().rootPath();
    return root ? root->string() : std::string{};
}

} // namespace

// 组件的拷贝表和序列化表由 engine::World 在构造时注册，这里不用管。
SceneDocument::SceneDocument(EditorContext& context)
        : m_context(&context) {}

SceneDocument::~SceneDocument() = default;

void SceneDocument::reset() {
    // 换场景前无条件退到 Edit（不管当前是 Simulate 还是 Play）：快照属于旧场景。
    m_context->exitToEdit();
    m_context->world().clear();
    m_context->clearSelection();
    m_file.clear();
    m_dirty = false;
}

void SceneDocument::createNew() {
    reset();
    populateDefault();
    log().info("New scene");
}

void SceneDocument::open() {
    const auto file = FileDialogs::openFile(kSceneFilter, projectRoot(*m_context));
    if (file.empty()) {
        return;
    }

    reset();
    load(file);
}

bool SceneDocument::load(const std::filesystem::path& path) {
    // 读失败时 World 会把场景清空并记下原因，这里只负责别留下一个指向没读进来的文件的文件名。
    if (!m_context->world().loadScene(path)) {
        m_file.clear();
        return false;
    }

    m_file = path;
    m_dirty = false;
    return true;
}

bool SceneDocument::save() {
    if (m_file.empty()) {
        return saveAs();
    }
    return write(m_file);
}

bool SceneDocument::saveAs() {
    const auto root = m_context->project().rootPath();
    const auto file = FileDialogs::saveFile(kSceneFilter,
            root ? (*root / "Untitled.artiscene").string() : std::string{});
    if (file.empty()) {
        return false;
    }

    auto with_extension = file;
    if (!with_extension.has_extension()) {
        with_extension.replace_extension(kSceneExtension);
    }
    return write(std::move(with_extension));
}

bool SceneDocument::write(std::filesystem::path path) {
    if (!m_context->world().saveScene(path)) {
        return false;
    }

    m_file = std::move(path);
    m_dirty = false;
    rememberInProject(m_file);
    return true;
}

void SceneDocument::rememberInProject(const std::filesystem::path& path) const {
    auto& projects = project::ProjectManager::instance();
    const auto& info = projects.getProjectInfo();
    if (!info) {
        return;
    }
    // 场景存在项目外面时记不进项目文件（见 relativeToProjectRoot）—— 不是错误，
    // 只是下次打开项目回不到它。
    const auto relative = relativeToProjectRoot(path);
    if (!relative) {
        return;
    }

    project::ProjectInfo updated = *info;
    updated.last_open_scene = *relative;
    projects.setProjectInfo(updated);
    projects.saveProject();
}

bool SceneDocument::loadLastOpen() {
    auto& projects = project::ProjectManager::instance();

    const auto& info = projects.getProjectInfo();
    if (!info || info->last_open_scene.empty()) {
        return false;
    }

    const auto root = projects.getProjectRootPath();
    if (!root) {
        return false;
    }

    const auto file = *root / info->last_open_scene;
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error) || error) {
        // 场景被删了或者项目是从别的机器拷过来的 —— 不是错误，只是没得可恢复。
        log().info("Last open scene '{}' does not exist, falling back to a default scene",
                info->last_open_scene.string());
        return false;
    }

    reset();
    if (!load(file)) {
        return false;
    }
    log().info("Restored the last open scene '{}'", info->last_open_scene.string());
    return true;
}

void SceneDocument::populateDefault() {
    const arti::asset::AssetHandle<engine::asset::MeshAsset> cube_mesh{
        engine::asset::kBuiltinCubeMesh
    };
    const arti::asset::AssetHandle<engine::asset::MaterialAsset> pbr_material{
        engine::asset::kBuiltinDefaultMaterial
    };

    auto& scene = m_context->scene();

    auto camera = scene.createEntity("Main Camera");
    camera.getComponent<scene::TransformComponent>().translation = glm::vec3{ 0.0f, 1.5f, 4.0f };
    camera.addComponent<engine::CameraComponent>();

    auto sun = scene.createEntity("Directional Light");
    sun.getComponent<scene::TransformComponent>().rotation =
            glm::quat{ glm::vec3{ glm::radians(-50.0f), glm::radians(-30.0f), 0.0f } };
    sun.addComponent<engine::DirectionalLightComponent>();

    // 点光源和聚光灯：默认场景里各放一个，这样新建场景就能直接看出这两种灯的行为
    // （1/d² 衰减、锥角落差），不用自己先加组件再摆位置。强度用组件的默认值 —— 那个值就是
    // 按「摆在这个距离上应该看得清」定的。
    auto point_light = scene.createEntity("Point Light");
    point_light.getComponent<scene::TransformComponent>().translation =
            glm::vec3{ -1.6f, 1.2f, 1.2f };
    auto& point = point_light.addComponent<engine::PointLightComponent>();
    point.color = glm::vec3{ 1.0f, 0.75f, 0.5f };
    point.range = 5.0f;

    auto spot_light = scene.createEntity("Spot Light");
    auto& spot_transform = spot_light.getComponent<scene::TransformComponent>();
    spot_transform.translation = glm::vec3{ 1.6f, 2.5f, 0.8f };
    // 绕 X 转 -90°：默认朝向是 -Z，转过来正好朝正下方。
    spot_transform.rotation = glm::quat{ glm::vec3{ glm::radians(-90.0f), 0.0f, 0.0f } };
    auto& spot = spot_light.addComponent<engine::SpotLightComponent>();
    spot.color = glm::vec3{ 0.6f, 0.8f, 1.0f };
    spot.range = 6.0f;

    auto environment = scene.createEntity("Environment");
    environment.addComponent<engine::EnvironmentComponent>();

    for (int index = -1; index <= 1; ++index) {
        auto cube = scene.createEntity("Cube");
        cube.getComponent<scene::TransformComponent>().translation =
                glm::vec3{ static_cast<float>(index) * 1.6f, 0.0f, 0.0f };
        auto& mesh_renderer = cube.addComponent<engine::MeshRendererComponent>();
        mesh_renderer.mesh = cube_mesh;
        mesh_renderer.materials.push_back(pbr_material);
    }

    log().info("Created default scene");
}

} // namespace arti::editor
