#pragma once
#include "artichoco/project/project_manager.h"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace arti::editor {

// 绝对路径 → 项目根相对路径。落在项目外面（相对路径里出现 ..）时返回 nullopt。
//
// ProjectInfo 里的路径都是项目根相对的，而 ProjectManager::setProjectInfo() 对带 .. 的
// 路径直接抛异常。所以凡是要写进 ProjectInfo 的路径都得先过这里 —— 场景存在项目外面
// 是完全合法的操作，只是它记不进项目文件。
inline std::optional<std::filesystem::path> relativeToProjectRoot(
        const std::filesystem::path& path) {
    const auto root = project::ProjectManager::instance().getProjectRootPath();
    if (!root || path.empty()) {
        return std::nullopt;
    }

    std::error_code error;
    const auto relative = std::filesystem::relative(path, *root, error);
    if (error || relative.empty()) {
        return std::nullopt;
    }
    for (const auto& component: relative) {
        if (component == "..") {
            return std::nullopt;
        }
    }
    return relative;
}

} // namespace arti::editor
