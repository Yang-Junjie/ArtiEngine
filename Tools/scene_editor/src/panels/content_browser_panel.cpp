#include "panels/content_browser_panel.h"

#include "editor_project.h"
#include "panels/ui_widgets.h"

#include "asset_tools/asset_pipeline.h"

#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "asset/prefab_asset.h"
#include "asset/texture_asset.h"

#include "artichoco/project/project_manager.h"

#include <algorithm>
#include <filesystem>
#include <imgui.h>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace arti::editor {
namespace {

constexpr ImVec4 kOkColor{ 0.60f, 0.90f, 0.70f, 1.00f };    // 已导入
constexpr ImVec4 kStaleColor{ 0.95f, 0.75f, 0.35f, 1.00f }; // 需要重导
constexpr ImVec4 kTypeColor{ 0.65f, 0.80f, 0.95f, 1.00f };  // 资产类型

// 树节点公用的一套 flag：整行可点、只有箭头和双击才展开。
constexpr ImGuiTreeNodeFlags kNodeFlags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                          ImGuiTreeNodeFlags_OpenOnArrow |
                                          ImGuiTreeNodeFlags_OpenOnDoubleClick;

// 已知类型给短名；未知类型把 type 原样显示 —— 它本身就是个可读的标识串。
std::string typeLabel(std::string_view type) {
    if (type == engine::asset::kMeshAssetType) {
        return "Mesh";
    }
    if (type == engine::asset::kMaterialAssetType) {
        return "Material";
    }
    if (type == engine::asset::kTextureAssetType) {
        return "Texture";
    }
    if (type == engine::asset::kPrefabAssetType) {
        return "Prefab";
    }
    return std::string{ type };
}

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// 资产节点的显示名就是它的 local_id（如 "mesh.helmet_LP"）。local_id 为空表示
// 源文件本身就是唯一产出，那时显示文件名。
std::string assetLabel(const arti::asset::AssetMetadata& metadata) {
    if (!metadata.local_id.empty()) {
        return metadata.local_id;
    }
    return metadata.source_path.filename().string();
}

// 预览栏的标题：名字用强调色，下面一行灰字说明它是什么。
void drawPreviewHeading(const std::string& name, const std::string& subtitle) {
    ImGui::PushStyleColor(ImGuiCol_Text, kAccentColor);
    ImGui::TextWrapped("%s", name.c_str());
    ImGui::PopStyleColor();
    ImGui::TextDisabled("%s", subtitle.c_str());
    ImGui::Separator();
}

} // namespace

ContentBrowserPanel::ContentBrowserPanel(EditorProject& project)
        : m_project(&project) {}

void ContentBrowserPanel::draw() {
    // 折叠或被裁掉时 Begin 返回 false，此时窗口的 SkipItems 为真：BeginTable 会失败，
    // 而后面的 TableNextRow 会解引用空表指针。所以这里必须提前收工。
    if (!ImGui::Begin("Content Browser")) {
        ImGui::End();
        return;
    }

    const auto assets_root = project::ProjectManager::instance().getAssetsRootPath();
    if (!assets_root) {
        drawEmptyState("Open a project to browse its assets");
        ImGui::End();
        return;
    }
    m_assets_root = *assets_root;

    // 两栏的高度必须在开表格之前量：进了单元格之后可用高度由内容决定，那时拿到 0，
    // 子窗口会缩成一条线。
    const float pane_height = ImGui::GetContentRegionAvail().y;
    constexpr ImGuiTableFlags split_flags =
            ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoPadOuterX;
    if (ImGui::BeginTable("##content_browser_split", 2, split_flags)) {
        ImGui::TableSetupColumn("##tree", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn("##preview", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableNextRow();

        // 两栏各自是一个子窗口，所以各自独立滚动。
        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("##tree_pane", ImVec2{ 0.0f, pane_height })) {
            drawTree();
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        if (ImGui::BeginChild("##preview_pane", ImVec2{ 0.0f, pane_height })) {
            drawPreview();
        }
        ImGui::EndChild();

        ImGui::EndTable();
    }

    ImGui::End();
}

std::vector<ContentBrowserPanel::Entry> ContentBrowserPanel::collectEntries(
        const std::filesystem::path& relative) const {
    std::vector<Entry> entries;
    std::error_code error;
    for (const auto& entry:
            std::filesystem::directory_iterator{ m_assets_root / relative, error }) {
        if (error) {
            break;
        }
        // .meta 是派生数据，和源文件一一对应，列出来只会让树长一倍。
        if (entry.path().extension() == arti::asset::kAssetMetadataExtension) {
            continue;
        }
        std::error_code entry_error;
        const bool is_directory = entry.is_directory(entry_error);
        std::filesystem::path child =
                relative.empty() ? entry.path().filename() : relative / entry.path().filename();
        entries.push_back({ std::move(child), entry.path().filename().string(), is_directory });
    }
    std::ranges::sort(entries, [](const Entry& left, const Entry& right) {
        if (left.is_directory != right.is_directory) {
            return left.is_directory;
        }
        return lower(left.name) < lower(right.name);
    });
    return entries;
}

void ContentBrowserPanel::drawTree() {
    // 每一级的缩进从默认的 21px 收到 12px。默认值是给「几层」的树设计的，
    // Assets/Model/DamagedHelmet/... 这种深度下光缩进就能吃掉小半个面板宽度。
    // 12px 仍然比箭头窄一点，层级看得出来，但不会一路往右跑。
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);

    // 根节点常开：Assets 底下就是全部内容，收起来这个面板就空了。
    if (ImGui::TreeNodeEx("Assets", kNodeFlags | ImGuiTreeNodeFlags_DefaultOpen)) {
        drawDirectoryChildren({});
        ImGui::TreePop();
    }
    drawEngineNode();

    ImGui::PopStyleVar();
}

void ContentBrowserPanel::drawDirectoryChildren(const std::filesystem::path& relative) {
    const std::vector<Entry> entries = collectEntries(relative);
    if (entries.empty()) {
        ImGui::TextDisabled("(empty)");
        return;
    }
    for (const Entry& entry: entries) {
        if (entry.is_directory) {
            drawDirectoryNode(entry);
        } else {
            drawSourceNode(entry);
        }
    }
}

void ContentBrowserPanel::drawDirectoryNode(const Entry& entry) {
    ImGui::PushID(entry.name.c_str());

    const bool open = ImGui::TreeNodeEx(entry.name.c_str(), kNodeFlags);

    if (open) {
        // 只有展开的目录才会枚举，所以每帧扫的目录数等于用户展开的数量。
        drawDirectoryChildren(entry.relative);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void ContentBrowserPanel::drawSourceNode(const Entry& entry) {
    // 按值拿：pipeline 的分组缓存会随 catalog revision 重建，引用会失效。
    const auto source = m_project->assetPipeline().sourceAssets(entry.relative);
    const bool expandable = !source.assets.empty();

    ImGui::PushID(entry.name.c_str());
    ImGuiTreeNodeFlags flags = kNodeFlags;
    if (!expandable) {
        // 叶子：不画箭头，但保留同样的缩进，名字仍然和上面对齐。
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    if (m_selected_source == entry.relative) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool open = ImGui::TreeNodeEx(entry.name.c_str(), flags);
    // 展开/收起不算选中，否则点箭头也会把预览栏切走。
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selectSource(entry.relative);
    }

    // 只给不正常的状态加标记。已导入是常态，每行都缀一句反而看不见重点；
    // 没有 importer 的文件（.bin、.artiscene）就是普通文件，也不用标。
    if (source.state == tools::asset::SourceState::Stale) {
        ImGui::SameLine();
        ImGui::TextColored(kStaleColor, "stale");
    } else if (source.state == tools::asset::SourceState::Pending) {
        ImGui::SameLine();
        ImGui::TextDisabled("not imported");
    }

    if (open) {
        for (const auto& asset: source.assets) {
            drawAssetNode(asset);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void ContentBrowserPanel::drawAssetNode(const arti::asset::AssetMetadata& metadata) {
    // handle 是 64 位的，截成 int 会撞车，所以走指针形式的 PushID。
    ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(metadata.handle.value())));

    const bool selected = m_selected_asset && *m_selected_asset == metadata.handle;
    const std::string label = assetLabel(metadata);
    const std::string type = typeLabel(metadata.type);

    // 资产没有子节点：Leaf 去掉箭头，NoTreePushOnOpen 表示不缩进也不需要 TreePop。
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf |
                               ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked()) {
        selectAsset(metadata.handle);
    }
    if (ImGui::BeginDragDropSource()) {
        const auto value = metadata.handle.value();
        ImGui::SetDragDropPayload(kAssetPayloadType, &value, sizeof(value));
        // 拖的时候写清楚放下去的是什么：一个 gltf 里好几个产出，只写文件名分不出来。
        ImGui::TextUnformatted(label.c_str());
        ImGui::SameLine();
        ImGui::TextColored(kTypeColor, "%s", type.c_str());
        ImGui::EndDragDropSource();
    }

    // 类型跟在名字后面。树只有一列，类型是这里最需要一眼看到的信息。
    ImGui::SameLine();
    ImGui::TextColored(kTypeColor, "%s", type.c_str());

    ImGui::PopID();
}

void ContentBrowserPanel::drawEngineNode() {
    const auto engine_assets = m_project->assetPipeline().engineAssets();
    if (engine_assets.empty()) {
        return;
    }
    const bool open = ImGui::TreeNodeEx("Engine", kNodeFlags | ImGuiTreeNodeFlags_DefaultOpen);
    // tooltip 要挂在 if 外面：放进 if 里的话，节点收起来时就没人注册它了。
    ImGui::SetItemTooltip("Built into the engine. No source file, read only.");
    if (open) {
        for (const auto& entry: engine_assets) {
            drawAssetNode(entry.metadata);
        }
        ImGui::TreePop();
    }
}

void ContentBrowserPanel::drawPreview() {
    if (m_selected_asset) {
        const auto metadata = m_project->assets().catalog().find(*m_selected_asset);
        if (!metadata) {
            // 重导入会换掉 handle，选中的那个可能已经不在 catalog 里了。
            drawEmptyState("This asset is no longer in the catalog");
            return;
        }
        drawAssetPreview(*metadata);
        return;
    }
    if (!m_selected_source.empty()) {
        drawSourcePreview();
        return;
    }
    drawEmptyState("Select an item to see its details");
}

void ContentBrowserPanel::drawAssetPreview(const arti::asset::AssetMetadata& metadata) {
    drawPreviewHeading(assetLabel(metadata), typeLabel(metadata.type));

    // 标签列比 Inspector 窄：这里的键都是 Type / Source 这种短词。
    if (!beginPropertyGrid("##cb_asset_info", 58.0f)) {
        return;
    }
    drawTextRow("Type", metadata.type.c_str());
    drawTextRow("Source", metadata.source_path.generic_string().c_str());
    if (!metadata.local_id.empty()) {
        drawTextRow("Local ID", metadata.local_id.c_str());
    }

    // UUID 这一行手写：值后面要跟一个复制按钮。
    propertyRow("UUID");
    const std::string uuid = metadata.handle.toString();
    ImGui::TextUnformatted(uuid.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        ImGui::SetClipboardText(uuid.c_str());
    }
    ImGui::SetItemTooltip("Copy the UUID to the clipboard");

    endPropertyGrid();
}

void ContentBrowserPanel::drawSourcePreview() {
    const auto source = m_project->assetPipeline().sourceAssets(m_selected_source);
    drawPreviewHeading(m_selected_source.filename().string(), "Source file");

    if (!beginPropertyGrid("##cb_source_info", 58.0f)) {
        return;
    }
    drawTextRow("Path", m_selected_source.generic_string().c_str());
    switch (source.state) {
        case tools::asset::SourceState::Imported:
            drawTextRowColored("Status", kOkColor, "imported");
            break;
        case tools::asset::SourceState::Stale:
            drawTextRowColored("Status", kStaleColor, "stale (artifact missing)");
            break;
        case tools::asset::SourceState::Pending:
            drawTextRow("Status", "not imported");
            break;
        case tools::asset::SourceState::Unsupported:
            drawTextRow("Status", "no importer");
            break;
    }
    drawTextRow("Assets", std::to_string(source.assets.size()).c_str());

    endPropertyGrid();
}

void ContentBrowserPanel::selectAsset(core::UUID asset) {
    m_selected_asset = asset;
    m_selected_source.clear();
}

void ContentBrowserPanel::selectSource(const std::filesystem::path& relative) {
    m_selected_source = relative;
    m_selected_asset.reset();
}

} // namespace arti::editor
