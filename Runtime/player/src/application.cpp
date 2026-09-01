#include "player_layer.h"

#include "artichoco/asset/asset_manifest.h"
#include "artichoco/core/application.h"
#include "artichoco/platform/window/window_factory.h"
#include "artichoco/project/project_manager.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

// 用法错误和配置错误分开：脚本里靠退出码区分「我调错了」和「项目坏了」。
constexpr int kUsageExit = 2;
constexpr int kConfigExit = 1;

void printUsage() {
    std::fputs("Usage:\n"
               "  arti_player [options] [<project.artiproj>]\n"
               "\n"
               "Options:\n"
               "  --project <file>  Project file to run. Same as the positional argument.\n"
               "                    Defaults to the single .artiproj next to the executable.\n"
               "  --scene <file>    Scene to load instead of the project's StartScene.\n"
               "                    Relative paths resolve against the project root.\n"
               "  --stats           Show the debug overlay (FPS, draw calls, entity count).\n"
               "  --help            Print this message.\n",
            stderr);
}

// ArtiChoco 的 createApplication 契约是「返回一个 Application」，没有地方放退出码 ——
// 返回 nullptr 会让 entry_point 再报一句 "Failed to create application instance."，盖在
// 我们自己的说明上面。参数和配置的问题就在这里就地退出：输出干净，退出码也对。
//
// 此时除了 Logger 什么都还没建起来，所以直接退出是安全的。
[[noreturn]] void exitWithUsage(int code) {
    printUsage();
    std::exit(code);
}

// 没给 --project 时的约定：exe 旁边那个 .artiproj。发布出去的游戏就长这样 —— 双击就跑，
// 不需要命令行。刻意要求「只有一个」：有两个就说不清该跑哪个，猜错比报错更糟。
std::optional<std::filesystem::path> findProjectBeside(const std::filesystem::path& directory) {
    std::error_code error;
    std::optional<std::filesystem::path> found;
    for (const auto& entry: std::filesystem::directory_iterator{ directory, error }) {
        if (error) {
            return std::nullopt;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".artiproj") {
            continue;
        }
        if (found) {
            std::fputs("More than one .artiproj sits next to the executable; "
                       "pass --project to pick one.\n",
                    stderr);
            return std::nullopt;
        }
        found = entry.path();
    }
    return found;
}

// 只做命令行层面的解析，不碰文件系统内容（除了找 exe 旁边的项目）。
arti::player::PlayerOptions parseOptions(int argc, char** argv) {
    arti::player::PlayerOptions options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{ argv[index] };

        if (argument == "--help" || argument == "-h") {
            // 求助不是错误：退出码 0。
            exitWithUsage(EXIT_SUCCESS);
        }
        if (argument == "--stats") {
            options.show_stats = true;
            continue;
        }
        // 需要值的选项：值缺了就是用法错误，不要静默当成开关。
        if (argument == "--project" || argument == "--scene") {
            if (index + 1 >= argc) {
                std::fprintf(stderr, "%.*s needs a path\n", static_cast<int>(argument.size()),
                        argument.data());
                exitWithUsage(kUsageExit);
            }
            auto& target = argument == "--project" ? options.project_file : options.scene_file;
            target = std::filesystem::path{ argv[++index] };
            continue;
        }
        if (argument.starts_with("-")) {
            std::fprintf(stderr, "Unknown option: %.*s\n", static_cast<int>(argument.size()),
                    argument.data());
            exitWithUsage(kUsageExit);
        }
        // 位置参数就是项目文件。给了两个说明用户搞错了，别猜。
        if (!options.project_file.empty()) {
            std::fputs("Only one project file can be given\n", stderr);
            exitWithUsage(kUsageExit);
        }
        options.project_file = std::filesystem::path{ argument };
    }

    if (options.project_file.empty()) {
        const std::filesystem::path executable{ argc > 0 ? argv[0] : "" };
        const auto beside = findProjectBeside(executable.parent_path());
        if (!beside) {
            std::fputs("No project file given and none found next to the executable.\n", stderr);
            exitWithUsage(kUsageExit);
        }
        options.project_file = *beside;
    }

    return options;
}

// 把要跑的场景定下来：--scene 优先，否则 ProjectInfo::StartScene。相对路径按项目根解析。
//
// 这一步刻意放在建 Vulkan 设备之前 —— 项目没配起始场景、或者场景文件被删了，是配置问题，
// 该立刻说清楚，而不是先花两秒起一个渲染器、开一个窗口，再在里面报错。
std::filesystem::path resolveScene(const std::filesystem::path& requested) {
    auto& projects = arti::project::ProjectManager::instance();
    const auto root = projects.getProjectRootPath();
    const auto& info = projects.getProjectInfo();
    if (!root || !info) {
        std::fputs("The project is not loaded.\n", stderr);
        std::exit(kConfigExit);
    }

    std::filesystem::path scene = requested;
    if (scene.empty()) {
        if (info->start_scene.empty()) {
            std::fputs("The project defines no StartScene, and no --scene was given.\n"
                       "Set one in the editor (Project Settings) or pass --scene <file>.\n",
                    stderr);
            std::exit(kConfigExit);
        }
        scene = info->start_scene;
    }
    if (scene.is_relative()) {
        scene = *root / scene;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(scene, error) || error) {
        std::fprintf(stderr, "Scene file does not exist: %s\n", scene.string().c_str());
        std::exit(kConfigExit);
    }
    return scene;
}

// 打包产物的标志：项目根下有一份 catalog.artimanifest。有它就走打包模式（catalog 从它建，
// 不碰 Assets/），没有就走开发模式（扫 Assets/ 下的 .meta）。
//
// 用「文件在不在」判定而不是加一个 --packaged 开关：双击 exe 的人不会传参数，而这两种模式
// 的区别恰好就是那个文件在不在。
std::filesystem::path findManifest() {
    const auto root = arti::project::ProjectManager::instance().getProjectRootPath();
    if (!root) {
        return {};
    }
    const auto manifest = *root / arti::asset::kAssetManifestFileName;
    std::error_code error;
    if (!std::filesystem::is_regular_file(manifest, error) || error) {
        return {};
    }
    return manifest;
}

} // namespace

namespace arti::core {

Application* createApplication(int argc, char** argv) {
    auto options = parseOptions(argc, argv);

    // 项目在建窗口之前加载：窗口标题要用项目名，而项目名在 .artiproj 里。
    // 加载失败时 ProjectManager 已经记了原因。
    if (!project::ProjectManager::instance().loadProject(options.project_file)) {
        std::fprintf(stderr, "Failed to load project: %s\n", options.project_file.string().c_str());
        std::exit(kConfigExit);
    }
    options.scene_file = resolveScene(options.scene_file);
    options.manifest_file = findManifest();

    const auto& project_info = project::ProjectManager::instance().getProjectInfo();

    ApplicationCreateInfo info;
    info.name = project_info ? project_info->name : std::string{ "ArtiEngine Player" };
    info.log_channel = "Player";
    info.width = 1'280;
    info.height = 720;
    info.window_factory = platform::createSDLWindow;

    auto app = std::make_unique<Application>(info);
    app->pushLayer(std::make_unique<player::PlayerLayer>(std::move(options)));
    return app.release();
}

} // namespace arti::core
