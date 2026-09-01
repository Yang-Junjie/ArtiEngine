#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace arti::tools::asset {

class AssetPipeline;

struct PackOptions {
    // 打包产物的目标目录。打包就是产出一个能整个拷走的目录，所以这个必须给。
    std::filesystem::path output_dir;

    // 目标目录里已经有东西时是否继续。默认拒绝：往一份旧产物上盖会留下上一次的资产，
    // 而残留在游戏里的表现是「删掉的东西还在」—— 那种 bug 没人会怀疑到打包这一步。
    bool overwrite{ false };

    // 打包前先跑一遍 reconcile，保证 artifact 是最新的。关掉它就是「相信当前 Library/」，
    // 适合 CI 里 scan 和 pack 分两步跑的情形。
    bool reconcile{ true };

    // 运行时文件（DLL、shaders/、播放器）的来源目录。调用方填 core::executableDir() ——
    // staging 已经把这些东西放在 asset_tools 自己旁边了。空表示只打资产、跳过运行时文件。
    //
    // 从「自己旁边」取而不是从源码树取：不需要知道 SDK 和源码树在哪，而且自动跟着构建配置走
    // （Debug 拿 SDL3d.dll，Release 拿 SDL3.dll）。
    std::filesystem::path runtime_dir;

    // 顺带把 arti_player 拷进产物，这样产物本身就能跑。
    // 找不到播放器时只记一条 warning、不算失败 —— CI 里 pack 和 player 可能分开构建。
    bool copy_player{ true };
};

struct PackReport {
    bool succeeded{ false };

    std::size_t assets{ 0 };            // 写进 manifest 的资产数（User）
    std::size_t artifacts_copied{ 0 };  // 拷过去的 artifact 数（含 builtin）
    std::size_t scenes_copied{ 0 };     // 拷过去的 .artiscene 数
    std::size_t runtime_files_copied{ 0 };  // DLL + shader + 播放器

    std::filesystem::path output_dir;
    std::filesystem::path project_file;
    std::filesystem::path manifest_file;

    std::vector<std::string> errors;
    // 不影响 succeeded 的问题。目前只有「没找到 arti_player」一种。
    std::vector<std::string> warnings;
};

// 把一个已经导好的项目打成可发布的目录：
//
//   <out>/<Name>.artiproj          项目文件，LastOpenScene 清掉
//   <out>/catalog.artimanifest     catalog 快照，运行时靠它建 catalog
//   <out>/Library/Artifacts/**     所有 artifact，含 builtin
//   <out>/Assets/**/*.artiscene    场景，只有场景
//   <out>/shaders/**               内建 .slang，着色器是运行期编译的
//   <out>/*.dll                    SDL3 / slang / slang-compiler
//   <out>/arti_player[.exe]        播放器本体（copy_player 时）
//
// 不含源模型、贴图，也不含任何 .meta。产物是自足的：整个目录拷到别的机器上双击就能跑，
// 不需要装 SDK、也不需要源码树在原位。
//
// 后三项来自 PackOptions::runtime_dir（调用方填 asset_tools 自己的 exe 目录）。
// runtime_dir 为空时只打资产 —— 那样的产物需要另外补运行时文件才能跑。
//
// 场景为什么还在 Assets/ 下面：场景现在不是资产（没有 handle、没有 artifact），
// 而 StartScene 是项目根相对路径。把 .artiscene 原位拷过去，那条路径就仍然成立，
// 不需要改写项目文件。场景变成真正的资产是另一件事。
//
// 前置条件：pipeline 已打开、ProjectManager 已加载对应项目。函数会先 checkIntegrity()，
// 缺 artifact 就整体失败 —— 打出一份少东西的包，比打包失败糟得多。
PackReport pack(AssetPipeline& pipeline, const PackOptions& options);

} // namespace arti::tools::asset
