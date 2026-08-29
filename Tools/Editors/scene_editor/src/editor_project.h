#pragma once
#include "artichoco/asset/asset_manager.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace arti::rendering {
class Renderer;
} // namespace arti::rendering

namespace arti::engine::asset {
class GPUAssetCache;
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

    // 按扩展名选 importer 导入一个源文件。path 相对 Assets 根目录。
    //
    // AssetManager::import 要求调用方指定 importer（它不按扩展名分发），而注册进去的
    // importer 列表也不对外暴露，所以这张扩展名表由这里维护。
    bool importFile(const std::filesystem::path& relative_path);

    // 扫 Assets/ 下所有还没有 .meta 的可识别文件并导入。打开项目后调一次 ——
    // 这样往 Assets 目录里拖一个 .obj、重开项目就能用。
    size_t importPending();

    // catalog 里已经有这个源文件（或它的任何子资产）了吗。
    bool isImported(const std::filesystem::path& relative_path) const;

    arti::asset::AssetManager& assets() noexcept { return m_assets; }
    engine::asset::GPUAssetCache& gpuAssets() noexcept { return *m_gpu_assets; }

    // 项目根目录，用于文件对话框的起始位置和场景路径解析。
    std::optional<std::filesystem::path> rootPath() const;

private:
    // 打开成功后的公共收尾：注册 loader、扫 .meta、确保内置资产在位。
    bool finishOpen();

    rendering::Renderer* m_renderer{ nullptr };
    // 扩展名 -> importer。指针的所有权在 m_assets 里。
    std::unordered_map<std::string, arti::asset::AssetImporter*> m_importers;
    arti::asset::AssetManager m_assets;
    std::unique_ptr<engine::asset::GPUAssetCache> m_gpu_assets;
    bool m_open{ false };
};

} // namespace arti::editor
