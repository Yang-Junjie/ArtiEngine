#pragma once
#include "artichoco/core/uuid.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace arti::asset {
struct AssetMetadata;
} // namespace arti::asset

namespace arti::editor {

class EditorProject;

// 项目 Assets/ 目录的简单浏览器：导航、导入状态、资产类型和 UUID。
//
// 列表是两个域的显式合并：文件系统提供"目录里有什么"，AssetCatalog 提供
// "这些是什么资产"。源文件行可展开，子资产各自成行、各自可拖拽 ——
// 拖拽在资产粒度，不再是"主资产"粒度。引擎自带资产在 Assets/ 下没有源文件，
// 单独一节展示。
class ContentBrowserPanel {
public:
    explicit ContentBrowserPanel(EditorProject& project);

    void draw();

    // 拖拽 payload 的数据类型。数据是 core::UUID::Value（一个 uint64_t）。
    static constexpr const char* kAssetPayloadType = "ARTI_ASSET_UUID";

private:
    // 需要用户填一个路径/名字的操作，用模态框收集输入后再执行。
    enum class PendingAction : uint8_t { None, Extract, Rename };

    void drawHeader();
    void drawDirectory(const std::filesystem::path& assets_root);
    void drawEngineAssets();
    // 选中源文件的导入设置。只对有 schema 的 importer 显示。
    void drawImportSettings();
    // 一个资产行（子资产或引擎资产）。返回 true 表示这一行被选中。
    void drawAssetRow(const arti::asset::AssetMetadata& metadata, bool nested);
    // 源文件行的右键菜单：重命名 / 删除 / 重导入。
    void drawSourceContextMenu(const std::filesystem::path& relative);
    // 派生资产行的右键菜单：目前只有 Extract（把它变成独立源文件）。
    void drawAssetContextMenu(const arti::asset::AssetMetadata& metadata);
    // Extract / Rename 的输入模态框。
    void drawPendingActionModal();

    EditorProject* m_project{ nullptr };
    std::filesystem::path m_current_dir;
    std::optional<core::UUID> m_selected_asset;
    // 选中的源文件（相对 Assets/），决定导入设置面板显示谁。
    std::filesystem::path m_selected_source;
    bool m_show_engine_assets{ true };

    PendingAction m_pending{ PendingAction::None };
    // 待操作的目标：Extract 用 handle，Rename 用源文件路径。
    core::UUID m_pending_asset;
    std::filesystem::path m_pending_source;
    std::string m_pending_input;
    std::string m_last_error;
};

} // namespace arti::editor
