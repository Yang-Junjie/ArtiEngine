#include "editor_project.h"

#include "artichoco/core/application.h"
#include "artichoco/project/project_manager.h"
#include "asset/gpu_asset_cache.h"

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

    close();

    if (!m_asset_pipeline.open(*assets_root, *artifacts_root)) {
        log().error("Failed to open the asset workspace at '{}'", assets_root->string());
        return false;
    }

    m_gpu_assets =
            std::make_unique<engine::asset::GPUAssetCache>(m_asset_pipeline.manager(), *m_renderer);
    m_open = true;

    const auto report = m_asset_pipeline.reconcile();
    if (report.imported > 0 || report.reimported > 0) {
        log().info("Reconcile imported {} and reimported {} source file(s)", report.imported,
                report.reimported);
    }
    if (report.forgotten > 0) {
        log().info("Reconcile forgot {} orphaned asset(s)", report.forgotten);
    }
    if (!report.succeeded()) {
        log().warn("Reconcile finished with {} failed file(s)", report.failed);
        for (const auto& error: report.errors) {
            log().warn("  {}", error);
        }
    }

    const auto& info = projects.getProjectInfo();
    log().info("Opened project '{}'", info ? info->name : std::string{ "?" });
    return true;
}

void EditorProject::close() {
    if (m_gpu_assets) {
        m_gpu_assets->clear();
        m_gpu_assets.reset();
    }
    m_asset_pipeline.close();
    m_open = false;
}

std::optional<std::filesystem::path> EditorProject::rootPath() const {
    return project::ProjectManager::instance().getProjectRootPath();
}

} // namespace arti::editor
