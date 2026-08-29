#include "editor_project.h"

#include "artichoco/core/application.h"
#include "artichoco/asset/asset_metadata.h"
#include "artichoco/project/project_manager.h"
#include "asset/builtin_assets.h"
#include "asset/gpu_asset_cache.h"
#include "asset/importers/gltf_importer.h"
#include "asset/importers/obj_importer.h"
#include "asset/importers/texture_importer.h"
#include "asset/loaders/material_loader.h"
#include "asset/loaders/mesh_loader.h"
#include "asset/loaders/prefab_loader.h"
#include "asset/loaders/texture_loader.h"

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
    m_assets.registerLoader(std::make_unique<engine::asset::TextureLoader>());
    m_assets.registerLoader(std::make_unique<engine::asset::PrefabLoader>());

    {
        auto obj = std::make_unique<engine::asset::ObjImporter>();
        auto* raw = obj.get();
        if (m_assets.registerImporter(std::move(obj))) {
            for (const auto& extension: raw->getSupportedExtensions()) {
                m_importers.emplace(extension, raw);
            }
        }
    }
    {
        auto gltf = std::make_unique<engine::asset::GltfImporter>();
        auto* raw = gltf.get();
        if (m_assets.registerImporter(std::move(gltf))) {
            for (const auto& extension: raw->getSupportedExtensions()) {
                m_importers.emplace(extension, raw);
            }
        }
    }
    {
        auto texture = std::make_unique<engine::asset::TextureImporter>();
        auto* raw = texture.get();
        if (m_assets.registerImporter(std::move(texture))) {
            for (const auto& extension: raw->getSupportedExtensions()) {
                m_importers.emplace(extension, raw);
            }
        }
    }

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

    m_gpu_assets = std::make_unique<engine::asset::GPUAssetCache>(m_assets, *m_renderer);
    m_open = true;

    // 扫一遍 Assets/：往那个目录里丢一个 .obj，重开项目就能用。
    if (const size_t imported = importPending(); imported > 0) {
        log().info("Imported {} pending source file(s)", imported);
    }

    const auto& info = projects.getProjectInfo();
    log().info("Opened project '{}'", info ? info->name : std::string{ "?" });
    return true;
}

bool EditorProject::importFile(const std::filesystem::path& relative_path) {
    auto extension = relative_path.extension().string();
    std::ranges::transform(extension, extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const auto importer = m_importers.find(extension);
    if (importer == m_importers.end()) {
        return false;
    }

    const auto results = m_assets.import(relative_path, *importer->second);
    bool ok = false;
    for (const auto& result: results) {
        if (!result) {
            log().error("Import of '{}' failed: {}", relative_path.string(), result.error);
            continue;
        }
        ok = true;
        log().info("Imported '{}' -> {} asset(s)", relative_path.string(), result.outputs.size());
    }
    return ok;
}

bool EditorProject::isImported(const std::filesystem::path& relative_path) const {
    // 子资产的 source_path 是「源路径 + suffix」，所以精确匹配和前缀匹配都算导过。
    const auto source = relative_path.generic_string();
    for (const auto& metadata: m_assets.catalog().allMetadata()) {
        const auto candidate = metadata.source_path.generic_string();
        if (candidate == source || (candidate.size() > source.size() &&
                                          candidate.starts_with(source) &&
                                          candidate[source.size()] == '.')) {
            return true;
        }
    }
    return false;
}

size_t EditorProject::importPending() {
    const auto assets_root = project::ProjectManager::instance().getAssetsRootPath();
    if (!assets_root) {
        return 0;
    }

    size_t imported = 0;
    std::error_code error;
    for (const auto& entry:
            std::filesystem::recursive_directory_iterator{ *assets_root, error }) {
        if (error) {
            break;
        }
        if (!entry.is_regular_file(error) || error) {
            continue;
        }

        const auto& path = entry.path();
        if (path.extension() == arti::asset::kAssetMetadataExtension) {
            continue;
        }

        const auto relative = std::filesystem::relative(path, *assets_root, error);
        if (error || relative.empty()) {
            continue;
        }
        // 查 catalog 而不是找 "<源文件>.meta"：复合导入的子资产 source_path 带 suffix
        // （box.obj.mesh.0），所以 box.obj.meta 根本不存在，按文件判断会每次都重导一遍。
        if (isImported(relative)) {
            continue;
        }
        if (importFile(relative)) {
            ++imported;
        }
    }
    return imported;
}

void EditorProject::close() {
    if (m_gpu_assets) {
        m_gpu_assets->clear();
        m_gpu_assets.reset();
    }
    m_assets.close();
    m_importers.clear();
    m_open = false;
}

std::optional<std::filesystem::path> EditorProject::rootPath() const {
    return project::ProjectManager::instance().getProjectRootPath();
}

} // namespace arti::editor
