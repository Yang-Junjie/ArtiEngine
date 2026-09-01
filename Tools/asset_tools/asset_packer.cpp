#include "asset_tools/asset_packer.h"

#include "asset_tools/asset_pipeline.h"

#include "artichoco/asset/asset_manifest.h"
#include "artichoco/project/project_manager.h"

#include <fstream>
#include <string>
#include <string_view>
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

// Windows 上播放器带 .exe，别的平台不带。
#if defined(_WIN32)
constexpr std::string_view kPlayerFileName{ "arti_player.exe" };
#else
constexpr std::string_view kPlayerFileName{ "arti_player" };
#endif

// 运行时文件：DLL + shaders/ + 播放器。来源是 options.runtime_dir，也就是 asset_tools 自己
// 所在的目录 —— staging 已经把这些东西放在那里了（见 artichoco_stage_vulkan_sdk_runtime 和
// artirenderer_stage_shaders）。
//
// 缺 DLL 或缺 shaders/ 算失败：那样的产物一定跑不起来，而「打出一份跑不起来的包」比打包
// 失败糟得多 —— 和上面 checkIntegrity() 的态度一致。
bool copyRuntimeFiles(const PackOptions& options, const std::filesystem::path& output_dir,
        PackReport& report) {
    std::error_code error;
    const auto runtime_dir = std::filesystem::absolute(options.runtime_dir, error);
    if (error || !std::filesystem::is_directory(runtime_dir, error) || error) {
        fail(report, "the runtime directory does not exist: " + options.runtime_dir.string());
        return false;
    }

    // DLL 按扩展名通配，不硬编码文件名：Debug 和 Release 的名字不一样（SDL3d.dll /
    // SDL3.dll），而 slang.dll 动态加载的 slang-compiler.dll 也在这一批里。
    std::size_t dll_count = 0;
    std::filesystem::directory_iterator entries{ runtime_dir, error };
    const std::filesystem::directory_iterator entries_end;
    if (error) {
        fail(report, "failed to scan '" + runtime_dir.string() + "': " + error.message());
        return false;
    }
    for (; !error && entries != entries_end; entries.increment(error)) {
        if (!entries->is_regular_file(error) || error) {
            continue;
        }
        if (entries->path().extension() != ".dll") {
            continue;
        }
        if (!copyOneFile(entries->path(), output_dir / entries->path().filename(), report)) {
            return false;
        }
        ++dll_count;
    }
    if (error) {
        fail(report, "failed while scanning for runtime libraries: " + error.message());
        return false;
    }
#if defined(_WIN32)
    // 非 Windows 上运行时依赖走 rpath，目录里本来就没有 .dll，所以这条只在 Windows 上是错误。
    if (dll_count == 0) {
        fail(report, "no runtime library found in '" + runtime_dir.string() +
                        "'; build the tools first so that staging has run");
        return false;
    }
#endif
    report.runtime_files_copied += dll_count;

    // 着色器整目录拷。运行期编译，所以缺一个就是一条画不出来的管线；而且 .slang 之间有
    // #include，半份目录比没有目录更难查。
    const auto shader_dir = runtime_dir / "shaders";
    if (!std::filesystem::is_directory(shader_dir, error) || error) {
        fail(report, "no 'shaders' directory next to the packer (" + shader_dir.string() +
                        "); build the tools first so that staging has run");
        return false;
    }
    std::size_t shader_count = 0;
    std::filesystem::recursive_directory_iterator shaders{ shader_dir,
        std::filesystem::directory_options::skip_permission_denied, error };
    const std::filesystem::recursive_directory_iterator shaders_end;
    while (!error && shaders != shaders_end) {
        if (shaders->is_regular_file(error) && !error) {
            const auto relative = std::filesystem::relative(shaders->path(), shader_dir, error);
            if (error) {
                break;
            }
            if (!copyOneFile(shaders->path(), output_dir / "shaders" / relative, report)) {
                return false;
            }
            ++shader_count;
        }
        shaders.increment(error);
    }
    if (error) {
        fail(report, "failed while copying shaders: " + error.message());
        return false;
    }
    if (shader_count == 0) {
        fail(report, "the 'shaders' directory next to the packer is empty");
        return false;
    }
    report.runtime_files_copied += shader_count;

    if (!options.copy_player) {
        return true;
    }
    // 播放器缺了只提示：CI 里 pack 和 player 可能分开构建，那种产物补一个 exe 就能用。
    const auto player = runtime_dir / kPlayerFileName;
    if (!std::filesystem::is_regular_file(player, error) || error) {
        report.warnings.push_back("no '" + std::string{ kPlayerFileName } + "' next to the packer; "
                "copy it into the output directory by hand to make the build runnable");
        return true;
    }
    if (!copyOneFile(player, output_dir / kPlayerFileName, report)) {
        return false;
    }
    ++report.runtime_files_copied;
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

    // 运行时文件放在最后：前面任何一步失败都不会留下一个「有 exe 没资产」的半成品目录。
    // runtime_dir 为空表示调用方明确只要资产（CLI 的 --no-runtime）。
    if (!options.runtime_dir.empty() && !copyRuntimeFiles(options, output_dir, report)) {
        return report;
    }

    report.succeeded = true;
    return report;
}

} // namespace arti::tools::asset
