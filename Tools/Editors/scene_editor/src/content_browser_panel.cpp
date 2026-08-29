#include "content_browser_panel.h"

#include "editor_project.h"

#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "asset/prefab_asset.h"
#include "asset/texture_asset.h"

#include "artichoco/project/project_manager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <imgui.h>
#include <system_error>
#include <vector>

namespace arti::editor {
namespace {

std::string_view typeLabel(std::string_view type) {
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
    return "Asset";
}

// 复合导入（.obj 会产出 mesh / material / texture / prefab 一篮子子资产）时
// 决定哪一份作为这个源文件的「主资产」：prefab 最完整，其次是 mesh。
int typePriority(std::string_view type) {
    if (type == engine::asset::kPrefabAssetType) {
        return 3;
    }
    if (type == engine::asset::kMeshAssetType) {
        return 2;
    }
    if (type == engine::asset::kMaterialAssetType) {
        return 1;
    }
    if (type == engine::asset::kTextureAssetType) {
        return 0;
    }
    return -1;
}

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

ContentBrowserPanel::ContentBrowserPanel(EditorProject& project)
        : m_project(&project) {}

void ContentBrowserPanel::draw() {
    ImGui::Begin("Content Browser");

    const auto assets_root = project::ProjectManager::instance().getAssetsRootPath();
    if (!assets_root) {
        ImGui::TextDisabled("Open a project to browse its assets");
        ImGui::End();
        return;
    }

    // 目录可能被外部删掉（比如用户在资源管理器里移走了它），退回根目录。
    std::error_code error;
    if (!m_current_dir.empty() &&
            !std::filesystem::is_directory(*assets_root / m_current_dir, error)) {
        m_current_dir.clear();
    }

    drawHeader();
    drawDirectory(*assets_root);

    ImGui::End();
}

void ContentBrowserPanel::drawHeader() {
    if (ImGui::SmallButton("Refresh")) {
        // 编辑器开着的时候往 Assets/ 里丢了文件，点一下扫进来。
        m_project->importPending();
    }
    ImGui::SameLine();
    if (!m_current_dir.empty() && ImGui::SmallButton("Up")) {
        m_current_dir = m_current_dir.parent_path();
    }
    ImGui::SameLine();

    std::string breadcrumb = "Assets";
    if (!m_current_dir.empty()) {
        breadcrumb += "/";
        breadcrumb += m_current_dir.generic_string();
    }
    ImGui::TextDisabled("%s", breadcrumb.c_str());
    ImGui::Separator();
}

void ContentBrowserPanel::drawDirectory(const std::filesystem::path& assets_root) {
    struct Entry {
        std::filesystem::path relative;
        std::string name;
        bool is_directory{ false };
    };

    std::vector<Entry> entries;
    std::error_code error;
    for (const auto& entry: std::filesystem::directory_iterator{ assets_root / m_current_dir,
                 error }) {
        if (error) {
            break;
        }
        if (entry.path().extension() == arti::asset::kAssetMetadataExtension) {
            continue;
        }
        const bool is_directory = entry.is_directory(error);
        const std::filesystem::path relative =
                m_current_dir.empty() ? entry.path().filename()
                                      : m_current_dir / entry.path().filename();
        entries.push_back({ std::move(relative), entry.path().filename().string(),
            is_directory });
    }
    std::ranges::sort(entries, [](const Entry& left, const Entry& right) {
        if (left.is_directory != right.is_directory) {
            return left.is_directory;
        }
        return lower(left.name) < lower(right.name);
    });

    if (!ImGui::BeginTable("##ContentBrowserFiles", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        return;
    }
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("UUID", ImGuiTableColumnFlags_WidthFixed, 170.0f);
    ImGui::TableHeadersRow();

    for (const Entry& entry: entries) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (entry.is_directory) {
            if (ImGui::Selectable((entry.name + "/").c_str())) {
                m_current_dir = entry.relative;
            }
            continue;
        }

        const auto info = assetInfoFor(entry.relative);
        const bool selected = info.imported && m_selected_asset &&
                              *m_selected_asset == info.primary_handle;
        if (ImGui::Selectable(entry.name.c_str(), selected,
                    ImGuiSelectableFlags_SpanAllColumns)) {
            m_selected_asset = info.imported ? std::optional{ info.primary_handle }
                                             : std::nullopt;
        }
        if (info.imported) {
            if (ImGui::BeginDragDropSource()) {
                const auto value = info.primary_handle.value();
                ImGui::SetDragDropPayload(kAssetPayloadType, &value, sizeof(value));
                ImGui::Text("%s", entry.name.c_str());
                ImGui::EndDragDropSource();
            }
        }
        if (ImGui::IsItemHovered() && info.imported) {
            ImGui::SetTooltip("%s\n%s\nUUID: %s", entry.name.c_str(), info.type_label.c_str(),
                    info.primary_handle.toString().c_str());
        }

        ImGui::TableSetColumnIndex(1);
        if (info.imported) {
            ImGui::TextColored(ImVec4{ 0.6f, 0.9f, 0.7f, 1.0f }, "%s", info.type_label.c_str());
        } else {
            ImGui::TextDisabled("not imported");
            ImGui::SameLine();
            if (ImGui::SmallButton("Import")) {
                m_project->importFile(entry.relative);
            }
        }

        ImGui::TableSetColumnIndex(2);
        if (info.imported) {
            const auto uuid = info.primary_handle.toString();
            ImGui::TextUnformatted(uuid.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to copy the UUID");
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                ImGui::SetClipboardText(uuid.c_str());
            }
        }
    }
    ImGui::EndTable();
}

ContentBrowserPanel::FileAssetInfo ContentBrowserPanel::assetInfoFor(
        const std::filesystem::path& relative) const {
    FileAssetInfo info;
    int best_priority = -2;
    const auto source = relative.generic_string();
    for (const auto& metadata: m_project->assets().catalog().allMetadata()) {
        const auto candidate = metadata.source_path.generic_string();
        const bool exact = candidate == source;
        const bool sub_asset = !exact && candidate.size() > source.size() &&
                               candidate.starts_with(source) && candidate[source.size()] == '.';
        if (!exact && !sub_asset) {
            continue;
        }
        info.imported = true;
        // 复合导入时按优先级挑一份当主资产；非标准类型也能被收录（-2 兜底）。
        if (const int priority = typePriority(metadata.type); priority > best_priority) {
            best_priority = priority;
            info.primary_handle = metadata.handle;
            info.type_label = std::string{ typeLabel(metadata.type) };
        }
    }
    return info;
}

} // namespace arti::editor
