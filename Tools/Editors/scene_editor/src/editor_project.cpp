#include "editor_project.h"

#include "artichoco/core/application.h"
#include "artichoco/project/project_manager.h"
#include "asset/builtin_assets.h"
#include "asset/gpu_asset_cache.h"
#include "asset/loaders/material_loader.h"
#include "asset/loaders/mesh_loader.h"

namespace arti::editor {
namespace {

const core::Logger::Channel& log() { return core::Application::get().getLogChannel(); }

} // namespace

EditorProject::EditorProject(rendering::Renderer& renderer) noexcept
        : m_renderer(&renderer) {}

EditorProject::~EditorProject() = default;

bool EditorProject::create(const std::filesystem::path& root, const std::string& name) {
    project::ProjectInfo info;
    info.name = name;
    // assets_path / artifacts_path 用 ProjectInfo 的默认值（Assets 和 Library/Artifacts）。

    if (!project::ProjectManager::instance().createProject(root, info)) {
        log().error("Failed to create the project at '{}'", root.string());
        return false;
    }
    return finishOpen();
}

bool EditorProject::open(const std::filesystem::path& project_file) {
    if (!project::ProjectManager::instance().loadProject(project_file)) {
        log().error("Failed to load the project '{}'", project_file.string());
        return false;
    }
    return finishOpen();
}

bool EditorProject::finishOpen() {
    auto& projects = project::ProjectManager::instance();
    const auto assets_root = projects.getAssetsRootPath();
    const auto artifacts_root = projects.getArtifactsRootPath();
    if (!assets_root || !artifacts_root) {
        log().error("The project has no assets or artifacts root");
        return false;
    }

    // 换项目要先丢掉上一份的 GPU 资源和工作区，否则会拿旧 UUID 查新 catalog。
    close();

    if (!m_assets.open(*assets_root, *artifacts_root)) {
        log().error("Failed to open the asset workspace at '{}'", assets_root->string());
        return false;
    }

    // loader 注册在 open 之后、扫描之前。没有 loader 的类型 load() 会失败，
    // 而内置资产要靠 load 验证是否可用。
    m_assets.registerLoader(std::make_unique<engine::asset::MeshLoader>());
    m_assets.registerLoader(std::make_unique<engine::asset::MaterialLoader>());

    // 把磁盘上已有的 .meta 读进 catalog。内置资产上次写下的也在这里被读回来，
    // 所以 ensureBuiltinAssets 会走「已存在」的分支。
    if (const auto scanned = m_assets.storage().scanMetadata()) {
        for (auto& metadata: *scanned) {
            m_assets.catalog().insert(std::move(metadata));
        }
        log().info("Scanned {} asset(s) from '{}'", m_assets.catalog().importedCount(),
                assets_root->string());
    }

    if (!engine::asset::ensureBuiltinAssets(m_assets)) {
        log().error("Failed to write the builtin assets");
        return false;
    }

    m_gpu_assets = std::make_unique<engine::asset::GpuAssetCache>(m_assets, *m_renderer);
    m_open = true;

    const auto& info = projects.getProjectInfo();
    log().info("Opened project '{}'", info ? info->name : std::string{ "?" });
    return true;
}

void EditorProject::close() {
    if (m_gpu_assets) {
        m_gpu_assets->clear();
        m_gpu_assets.reset();
    }
    m_assets.close();
    m_open = false;
}

std::optional<std::filesystem::path> EditorProject::rootPath() const {
    return project::ProjectManager::instance().getProjectRootPath();
}

} // namespace arti::editor
