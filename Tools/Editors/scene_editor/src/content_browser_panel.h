#pragma once
#include "artichoco/core/uuid.h"

#include <filesystem>
#include <optional>
#include <string>

namespace arti::editor {

class EditorProject;

// 项目 Assets/ 目录的简单浏览器：导航、导入状态、资产类型和 UUID。
// 已导入的资产行支持拖拽，EditorLayer 在 Viewport 上接住落点生成场景实体。
class ContentBrowserPanel {
public:
    explicit ContentBrowserPanel(EditorProject& project);

    void draw();

    // 拖拽 payload 的数据类型。数据是 core::UUID::Value（一个 uint64_t）。
    static constexpr const char* kAssetPayloadType = "ARTI_ASSET_UUID";

private:
    struct FileAssetInfo {
        bool imported{ false };
        std::string type_label;
        core::UUID primary_handle;
    };

    void drawHeader();
    void drawDirectory(const std::filesystem::path& assets_root);
    FileAssetInfo assetInfoFor(const std::filesystem::path& relative) const;

    EditorProject* m_project{ nullptr };
    std::filesystem::path m_current_dir;
    std::optional<core::UUID> m_selected_asset;
};

} // namespace arti::editor
