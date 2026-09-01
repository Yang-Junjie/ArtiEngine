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
};

struct PackReport {
    bool succeeded{ false };

    std::size_t assets{ 0 };            // 写进 manifest 的资产数（User）
    std::size_t artifacts_copied{ 0 };  // 拷过去的 artifact 数（含 builtin）
    std::size_t scenes_copied{ 0 };     // 拷过去的 .artiscene 数

    std::filesystem::path output_dir;
    std::filesystem::path project_file;
    std::filesystem::path manifest_file;

    std::vector<std::string> errors;
};

// 把一个已经导好的项目打成可发布的目录：
//
//   <out>/<Name>.artiproj          项目文件，LastOpenScene 清掉
//   <out>/catalog.artimanifest     catalog 快照，运行时靠它建 catalog
//   <out>/Library/Artifacts/**     所有 artifact，含 builtin
//   <out>/Assets/**/*.artiscene    场景，只有场景
//
// 不含源模型、贴图，也不含任何 .meta。把 arti_player.exe 拷进 <out>/ 就能双击运行。
//
// 场景为什么还在 Assets/ 下面：场景现在不是资产（没有 handle、没有 artifact），
// 而 StartScene 是项目根相对路径。把 .artiscene 原位拷过去，那条路径就仍然成立，
// 不需要改写项目文件。场景变成真正的资产是另一件事。
//
// 前置条件：pipeline 已打开、ProjectManager 已加载对应项目。函数会先 checkIntegrity()，
// 缺 artifact 就整体失败 —— 打出一份少东西的包，比打包失败糟得多。
PackReport pack(AssetPipeline& pipeline, const PackOptions& options);

} // namespace arti::tools::asset
