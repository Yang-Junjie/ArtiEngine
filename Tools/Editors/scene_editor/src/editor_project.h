#pragma once
#include "artichoco/asset/asset_manager.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace arti::rendering {
class Renderer;
} // namespace arti::rendering

namespace arti::engine::asset {
class GpuAssetCache;
} // namespace arti::engine::asset

namespace arti::editor {

// 编辑器打开的项目。持有 AssetManager 和 GpuAssetCache —— 两者的生命周期都绑在项目上：
// 换项目要重新扫 .meta、重新上传 GPU 资源。
//
// ProjectManager 是全局单例（ArtiChoco 的设计），但资产工作区不是，所以这里持有它们。
class EditorProject {
public:
    explicit EditorProject(rendering::Renderer& renderer) noexcept;
    ~EditorProject();

    EditorProject(const EditorProject&) = delete;
    EditorProject& operator=(const EditorProject&) = delete;

    // 在 root 下建项目（写 .artiproj、建 Assets 和 Library/Artifacts），然后打开它。
    bool create(const std::filesystem::path& root, const std::string& name);

    // 打开已有项目文件。
    bool open(const std::filesystem::path& project_file);

    void close();

    bool isOpen() const noexcept { return m_open; }

    arti::asset::AssetManager& assets() noexcept { return m_assets; }
    engine::asset::GpuAssetCache& gpuAssets() noexcept { return *m_gpu_assets; }

    // 项目根目录，用于文件对话框的起始位置和场景路径解析。
    std::optional<std::filesystem::path> rootPath() const;

private:
    // 打开成功后的公共收尾：注册 loader、扫 .meta、确保内置资产在位。
    bool finishOpen();

    rendering::Renderer* m_renderer{ nullptr };
    arti::asset::AssetManager m_assets;
    std::unique_ptr<engine::asset::GpuAssetCache> m_gpu_assets;
    bool m_open{ false };
};

} // namespace arti::editor
