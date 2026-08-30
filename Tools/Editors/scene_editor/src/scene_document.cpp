#include "scene_document.h"

#include "editor_context.h"
#include "editor_project.h"

#include "platform/common/file_dialogs.h"

#include "asset/builtin_assets.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "scene/component_registration.h"
#include "scene/components.h"

#include "artichoco/core/application.h"
#include "artichoco/project/project_manager.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"
#include "artichoco/scene/scene_serializer.h"

#include <glm/gtc/quaternion.hpp>

#include <exception>
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

SceneDocument::SceneDocument(EditorContext& context)
        : m_context(&context) {
    m_serialization = std::make_unique<scene::SceneSerializationRegistry>();
    engine::registerSceneComponents(m_serialization.get());
    m_serializer = std::make_unique<scene::SceneSerializer>(*m_serialization);
}

SceneDocument::~SceneDocument() = default;

void SceneDocument::reset() {
    m_context->exitPlayMode();
    m_context->scene().clearEntities();
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
    try {
        m_serializer->load(path, m_context->scene());
    } catch (const std::exception& exception) {
        log().error("Failed to load the scene '{}': {}", path.string(), exception.what());
        m_context->scene().clearEntities();
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
    try {
        m_serializer->save(m_context->scene(), path);
    } catch (const std::exception& exception) {
        log().error("Failed to save the scene '{}': {}", path.string(), exception.what());
        return false;
    }

    m_file = std::move(path);
    m_dirty = false;
    rememberInProject(m_file);
    return true;
}

void SceneDocument::rememberInProject(const std::filesystem::path& path) const {
    // ProjectInfo::last_open_scene 是相对项目根的
    auto& projects = project::ProjectManager::instance();
    const auto& info = projects.getProjectInfo();
    if (!info) {
        return;
    }
    const auto root = projects.getProjectRootPath();
    if (!root) {
        return;
    }

    std::error_code error;
    const auto relative = std::filesystem::relative(path, *root, error);
    if (error || relative.empty()) {
        return;
    }
    // 场景存在项目外面时 relative 会带 ..，那种路径 ProjectManager 会拒，所以只在
    // 确实位于项目内时才记。
    if (relative.generic_string().find("..") != std::string::npos) {
        return;
    }

    project::ProjectInfo updated = *info;
    updated.last_open_scene = relative;
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
