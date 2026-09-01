#pragma once
#include "artichoco/core/uuid.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace arti::asset {
struct AssetMetadata;
} // namespace arti::asset

namespace arti::editor {

class EditorProject;

// 项目 Assets/ 目录的浏览器。左边一棵树，右边选中项的预览。
//
// 树是两个域的显式合并：文件系统提供"目录里有什么"，AssetCatalog 提供"这些是
// 什么资产"。所以层级是 目录 → 源文件 → 资产：源文件节点展开后是它导出的那些
// 资产，每个资产各自成行、各自可拖拽 —— 拖拽在资产粒度，不是"主资产"粒度。
// 引擎自带资产在 Assets/ 下没有源文件，挂在树末尾单独一个节点下。
//
// 没有"当前目录"这个概念：树整棵在，展开哪些由用户决定，所以也不需要面包屑或
// 返回上级。每帧只枚举被展开的目录，代价随可见范围走。
class ContentBrowserPanel {
public:
    explicit ContentBrowserPanel(EditorProject& project);

    void draw();

    // 拖拽 payload 的数据类型。数据是 core::UUID::Value（一个 uint64_t）。
    static constexpr const char* kAssetPayloadType = "ARTI_ASSET_UUID";

private:
    // 树里的一项，来自文件系统。
    struct Entry {
        std::filesystem::path relative; // 相对 Assets/
        std::string name;
        bool is_directory{ false };
    };

    // ---- 左边：树 ----
    void drawTree();
    // 一个目录下的全部条目。只有展开的节点才会调到这里。
    void drawDirectoryChildren(const std::filesystem::path& relative);
    void drawDirectoryNode(const Entry& entry);
    // 一个源文件节点，展开后是它导出的资产。
    void drawSourceNode(const Entry& entry);
    // 一个资产节点（源文件的子资产，或引擎自带资产）。可选、可拖。
    void drawAssetNode(const arti::asset::AssetMetadata& metadata);
    // 引擎自带资产：树末尾的一个节点，恒定显示。
    void drawEngineNode();

    // ---- 右边：预览 ----
    void drawPreview();
    void drawAssetPreview(const arti::asset::AssetMetadata& metadata);
    void drawSourcePreview();

    std::vector<Entry> collectEntries(const std::filesystem::path& relative) const;
    // 选中是二选一：选资产就清掉源文件，反之亦然，这样预览栏只有一个数据源。
    void selectAsset(core::UUID asset);
    void selectSource(const std::filesystem::path& relative);

    EditorProject* m_project{ nullptr };
    // 每帧从 ProjectManager 取一次，省得每个画图函数都多带一个参数。
    std::filesystem::path m_assets_root;

    std::optional<core::UUID> m_selected_asset;
    std::filesystem::path m_selected_source;
};

} // namespace arti::editor
