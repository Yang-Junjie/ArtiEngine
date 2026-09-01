#include "asset_tools/asset_packer.h"

#include "asset_tools/asset_pipeline.h"

#include "artichoco/asset/asset_manifest.h"
#include "artichoco/project/project_manager.h"

#include <fstream>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace arti::tools::asset {
namespace {

// 场景现在不是资产，所以打包只能按扩展名认它。和 SceneDocument 里那个是同一个约定。
constexpr std::string_view kSceneExtension{ ".artiscene" };

void fail(PackReport& report, std::string message) {
    report.succeeded = false;
    report.errors.push_back(std::move(message));
}

bool isEmptyOrMissing(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) {
        return true;
    }
    return std::filesystem::is_empty(directory, error) && !error;
}

// path 是否落在 root 里面（含 root 自身）。
bool isInside(const std::filesystem::path& path, const std::filesystem::path& root) {
    std::error_code error;
    const auto relative = std::filesystem::relative(path, root, error);
    if (error || relative.empty()) {
        return false;
    }
    for (const auto& component: relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

bool copyOneFile(const std::filesystem::path& from, const std::filesystem::path& to,
        PackReport& report) {
    std::error_code error;
    std::filesystem::create_directories(to.parent_path(), error);
    if (error) {
        fail(report, "failed to create '" + to.parent_path().string() + "': " + error.message());
        return false;
    }
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        fail(report, "failed to copy '" + from.string() + "': " + error.message());
        return false;
    }
    return true;
}

bool writeTextFile(const std::filesystem::path& file, const std::string& text,
        PackReport& report) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (error) {
        fail(report, "failed to create '" + file.parent_path().string() + "': " + error.message());
        return false;
    }
    std::ofstream output{ file, std::ios::binary | std::ios::trunc };
    if (!output.is_open()) {
        fail(report, "failed to open '" + file.string() + "' for writing");
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output.good()) {
        fail(report, "failed to write '" + file.string() + "'");
        return false;
    }
    return true;
}

} // namespace

PackReport pack(AssetPipeline& pipeline, const PackOptions& options) {
    PackReport report;

    if (!pipeline.isOpen()) {
        fail(report, "the asset workspace is not open");
        return report;
    }
    if (options.output_dir.empty()) {
        fail(report, "no output directory was given");
        return report;
    }

    auto& projects = project::ProjectManager::instance();
    const auto project_root = projects.getProjectRootPath();
    const auto assets_root = projects.getAssetsRootPath();
    const auto artifacts_root = projects.getArtifactsRootPath();
    const auto& info = projects.getProjectInfo();
    if (!project_root || !assets_root || !artifacts_root || !info) {
        fail(report, "no project is loaded");
        return report;
    }

    std::error_code error;
    const auto output_dir = std::filesystem::absolute(options.output_dir, error).lexically_normal();
    if (error) {
        fail(report, "failed to resolve the output directory: " + error.message());
        return report;
    }
    report.output_dir = output_dir;

    // 输出目录不能在项目里面：一边遍历 Library/Artifacts 一边往它下面写，行为取决于目录
    // 迭代器的实现；而且下一次打包会把上一次的产物当成项目内容扫进来。
    if (isInside(output_dir, *project_root)) {
        fail(report, "the output directory must be outside the project root");
        return report;
    }
    if (!options.overwrite && !isEmptyOrMissing(output_dir)) {
        fail(report, "the output directory is not empty (pass --overwrite to write into it anyway)");
        return report;
    }

    if (options.reconcile) {
        const auto reconciled = pipeline.reconcile();
        if (!reconciled.succeeded()) {
            fail(report, "reconcile failed before packing; fix the import errors first");
            for (const auto& message: reconciled.errors) {
                report.errors.push_back("  " + message);
            }
            return report;
        }
    }

    // 缺 artifact 就整体失败：这是「这个包是不是完整的」唯一的自动检查。
    const auto integrity = pipeline.checkIntegrity();
    if (!integrity.succeeded()) {
        fail(report, "the project fails its integrity check; packing would ship a broken build");
        for (const auto& issue: integrity.issues) {
            report.errors.push_back("  " + issue.handle.toString() + ": " + issue.message);
        }
        return report;
    }

    // 项目文件先写：createProject 会把 <out>、<out>/Assets 和 <out>/Library/Artifacts 建出来，
    // 正好是接下来要拷进去的三个位置。用一个局部 ProjectManager，不动进程里那个单例的状态。
    project::ProjectInfo packaged = *info;
    // LastOpenScene 是编辑器的书签，发布出去没有意义。
    packaged.last_open_scene.clear();
    project::ProjectManager staging;
    if (!staging.createProject(output_dir, packaged)) {
        fail(report, "failed to write the project file into the output directory");
        return report;
    }
    report.project_file = output_dir / (packaged.name + ".artiproj");

    // manifest 只装 User 条目（builtin 由运行时自己登记），但 artifact 要连 builtin 一起拷 ——
    // 装在只读目录里的游戏没法现场补写 builtin。
    const auto manifest = pipeline.manager().buildManifest();
    const auto text = arti::asset::serializeAssetManifest(manifest);
    if (!text) {
        fail(report, "failed to serialize the asset manifest");
        return report;
    }
    report.manifest_file = output_dir / arti::asset::kAssetManifestFileName;
    if (!writeTextFile(report.manifest_file, *text, report)) {
        return report;
    }
    report.assets = manifest.assets.size();

    const auto output_artifacts = output_dir / info->artifacts_path;
    std::unordered_set<std::string> copied;
    for (const auto& entry: pipeline.manager().catalog().allEntries()) {
        const auto relative = entry.metadata.artifact_path.lexically_normal();
        if (!copied.insert(relative.generic_string()).second) {
            continue;
        }
        if (!copyOneFile(*artifacts_root / relative, output_artifacts / relative, report)) {
            return report;
        }
        ++report.artifacts_copied;
    }

    // 场景：Assets/ 下所有 .artiscene，原位拷过去，别的一概不拷。
    const auto output_assets = output_dir / info->assets_path;
    std::filesystem::recursive_directory_iterator iterator{ *assets_root,
        std::filesystem::directory_options::skip_permission_denied, error };
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error) && iterator->path().extension() == kSceneExtension) {
            const auto relative = std::filesystem::relative(iterator->path(), *assets_root, error);
            if (error) {
                break;
            }
            if (!copyOneFile(iterator->path(), output_assets / relative, report)) {
                return report;
            }
            ++report.scenes_copied;
        }
        iterator.increment(error);
    }
    if (error) {
        fail(report, "failed while scanning for scenes: " + error.message());
        return report;
    }

    report.succeeded = true;
    return report;
}

} // namespace arti::tools::asset
