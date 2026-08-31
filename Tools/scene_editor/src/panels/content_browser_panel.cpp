#include "panels/content_browser_panel.h"

#include "editor_project.h"

#include "asset_tools/asset_pipeline.h"

#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "asset/prefab_asset.h"
#include "asset/texture_asset.h"

#include "artichoco/project/project_manager.h"

#include <algorithm>
#include <filesystem>
#include <imgui.h>
#include <string_view>
#include <system_error>
#include <vector>

namespace arti::editor {
namespace {

constexpr ImVec4 kAssetColor{ 0.6f, 0.9f, 0.7f, 1.0f };
constexpr ImVec4 kStaleColor{ 0.95f, 0.75f, 0.35f, 1.0f };
constexpr ImVec4 kEngineColor{ 0.65f, 0.8f, 0.95f, 1.0f };

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

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// 子资产的显示名就是它的 local_id（如 "mesh.helmet_LP"）。local_id 为空表示
// 源文件本身就是唯一产出，那时显示文件名。
std::string subAssetLabel(const arti::asset::AssetMetadata& metadata) {
    if (!metadata.local_id.empty()) {
        return metadata.local_id;
    }
    return metadata.source_path.filename().string();
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
    drawImportSettings();
    if (m_show_engine_assets && m_current_dir.empty()) {
        drawEngineAssets();
    }

    ImGui::End();
}

void ContentBrowserPanel::drawHeader() {
    if (ImGui::SmallButton("Reconcile")) {
        // 三方对账：导入新文件、重导 artifact 缺失的、清掉源文件已删的孤儿。
        m_project->assetPipeline().reconcile();
    }
    ImGui::SameLine();
    if (!m_current_dir.empty() && ImGui::SmallButton("Up")) {
        m_current_dir = m_current_dir.parent_path();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Engine", &m_show_engine_assets);
    ImGui::SameLine();

    std::string breadcrumb = "Assets";
    if (!m_current_dir.empty()) {
        breadcrumb += "/";
        breadcrumb += m_current_dir.generic_string();
    }
    ImGui::TextDisabled("%s", breadcrumb.c_str());
    ImGui::Separator();
}

void ContentBrowserPanel::drawAssetRow(const arti::asset::AssetMetadata& metadata, bool nested) {
    const bool selected = m_selected_asset && *m_selected_asset == metadata.handle;
    const std::string label = typeLabel(metadata.type) == std::string_view{ "Asset" }
                                      ? metadata.type
                                      : std::string{ typeLabel(metadata.type) };

    if (ImGui::Selectable("##asset", selected, ImGuiSelectableFlags_SpanAllColumns)) {
        m_selected_asset = metadata.handle;
    }
    if (ImGui::BeginDragDropSource()) {
        const auto value = metadata.handle.value();
        ImGui::SetDragDropPayload(kAssetPayloadType, &value, sizeof(value));
        ImGui::Text("%s", metadata.source_path.filename().string().c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\n%s\nUUID: %s", metadata.source_path.generic_string().c_str(),
                label.c_str(), metadata.handle.toString().c_str());
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::TextColored(nested ? kAssetColor : kEngineColor, "%s", label.c_str());

    ImGui::TableSetColumnIndex(2);
    const auto uuid = metadata.handle.toString();
    ImGui::TextUnformatted(uuid.c_str());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Click to copy the UUID");
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ImGui::SetClipboardText(uuid.c_str());
    }
}

void ContentBrowserPanel::drawDirectory(const std::filesystem::path& assets_root) {
    struct Entry {
        std::filesystem::path relative;
        std::string name;
        bool is_directory{ false };
    };

    std::vector<Entry> entries;
    std::error_code error;
    for (const auto& entry:
            std::filesystem::directory_iterator{ assets_root / m_current_dir, error }) {
        if (error) {
            break;
        }
        if (entry.path().extension() == arti::asset::kAssetMetadataExtension) {
            continue;
        }
        const bool is_directory = entry.is_directory(error);
        const std::filesystem::path relative = m_current_dir.empty()
                                                       ? entry.path().filename()
                                                       : m_current_dir / entry.path().filename();
        entries.push_back({ std::move(relative), entry.path().filename().string(), is_directory });
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
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("UUID", ImGuiTableColumnFlags_WidthFixed, 170.0f);
    ImGui::TableHeadersRow();

    auto& pipeline = m_project->assetPipeline();
    for (const Entry& entry: entries) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        if (entry.is_directory) {
            if (ImGui::Selectable((entry.name + "/").c_str())) {
                m_current_dir = entry.relative;
            }
            continue;
        }

        // 按值拿：下面可能调 importFile()，那会让 pipeline 的分组缓存重建。
        const auto source = pipeline.sourceAssets(entry.relative);
        const bool expandable = !source.assets.empty();

        ImGui::PushID(entry.name.c_str());
        bool open = false;
        if (expandable) {
            open = ImGui::TreeNodeEx("##source",
                    ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::SameLine();
        }
        const bool source_selected = m_selected_source == entry.relative;
        if (ImGui::Selectable(entry.name.c_str(), source_selected)) {
            m_selected_source = entry.relative;
        }

        ImGui::TableSetColumnIndex(1);
        switch (source.state) {
        case tools::asset::SourceState::Imported:
            ImGui::TextColored(kAssetColor, "%zu asset%s", source.assets.size(),
                    source.assets.size() == 1 ? "" : "s");
            ImGui::SameLine();
            // 目前没有源文件变更检测，所以改完源文件只能手动点这里重导。
            if (ImGui::SmallButton("Reimport")) {
                pipeline.importFile(entry.relative);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Re-run the importer for this source file");
            }
            break;
        case tools::asset::SourceState::Stale:
            ImGui::TextColored(kStaleColor, "stale");
            ImGui::SameLine();
            if (ImGui::SmallButton("Reimport")) {
                pipeline.importFile(entry.relative);
            }
            break;
        case tools::asset::SourceState::Pending:
            ImGui::TextDisabled("not imported");
            ImGui::SameLine();
            if (ImGui::SmallButton("Import")) {
                pipeline.importFile(entry.relative);
            }
            break;
        case tools::asset::SourceState::Unsupported:
            ImGui::TextDisabled("not importable");
            break;
        }

        if (open) {
            for (const auto& asset: source.assets) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(asset.handle.value()));
                ImGui::Indent();
                ImGui::TextUnformatted(subAssetLabel(asset).c_str());
                ImGui::SameLine();
                drawAssetRow(asset, true);
                ImGui::Unindent();
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void ContentBrowserPanel::drawImportSettings() {
    if (m_selected_source.empty()) {
        return;
    }
    auto& pipeline = m_project->assetPipeline();
    const auto settings = pipeline.sourceSettings(m_selected_source);
    if (!settings.valid || settings.schema.empty()) {
        return;
    }

    ImGui::Separator();
    const std::string header = "Import Settings: " + m_selected_source.filename().string();
    if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    for (const auto& descriptor: settings.schema) {
        const auto layer = settings.resolved.layerOf(descriptor.key);
        const bool authored = layer == arti::asset::SettingLayer::Authored;
        ImGui::PushID(descriptor.key.c_str());

        // 字符串枚举：下拉框。其余类型等有 importer 真的需要时再加。
        if (!descriptor.allowed.empty()) {
            const std::string& current = settings.resolved.getString(descriptor.key);
            if (ImGui::BeginCombo(descriptor.key.c_str(), current.c_str())) {
                for (const auto& option: descriptor.allowed) {
                    if (ImGui::Selectable(option.c_str(), option == current) &&
                            option != current) {
                        pipeline.setAuthoredSetting(m_selected_source, descriptor.key,
                                arti::asset::Value{ option });
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextDisabled("%s (unsupported editor type)", descriptor.key.c_str());
        }

        // 显式标出这个值来自哪一层：用户设的、容器推断的、还是默认。
        ImGui::SameLine();
        switch (layer) {
        case arti::asset::SettingLayer::Authored:
            ImGui::TextColored(kStaleColor, "authored");
            break;
        case arti::asset::SettingLayer::Inferred: {
            ImGui::TextColored(kEngineColor, "inferred");
            const auto found = settings.stored.inferred.find(descriptor.key);
            if (ImGui::IsItemHovered() && found != settings.stored.inferred.end()) {
                ImGui::SetTooltip("Inferred by %s%s%s", found->second.by.generic_string().c_str(),
                        found->second.usage.empty() ? "" : " as ", found->second.usage.c_str());
            }
            break;
        }
        case arti::asset::SettingLayer::Default:
            ImGui::TextDisabled("default");
            break;
        }

        if (authored) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset")) {
                // 清除用户设定，回落到 inferred / default。
                pipeline.setAuthoredSetting(m_selected_source, descriptor.key, std::nullopt);
            }
        }
        if (!descriptor.doc.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", descriptor.doc.c_str());
        }
        ImGui::PopID();
    }

    for (const auto& issue: settings.resolved.issues()) {
        ImGui::TextColored(kStaleColor, "%s: %s", issue.key.c_str(), issue.detail.c_str());
    }
}

void ContentBrowserPanel::drawEngineAssets() {
    const auto engine_assets = m_project->assetPipeline().engineAssets();
    if (engine_assets.empty()) {
        return;
    }

    ImGui::Separator();
    if (!ImGui::CollapsingHeader("Engine Assets", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::TextDisabled("Built into the engine. No source file, read only.");

    if (!ImGui::BeginTable("##ContentBrowserEngine", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        return;
    }
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("UUID", ImGuiTableColumnFlags_WidthFixed, 170.0f);
    ImGui::TableHeadersRow();

    for (const auto& entry: engine_assets) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(static_cast<int>(entry.metadata.handle.value()));
        ImGui::TextUnformatted(entry.metadata.source_path.generic_string().c_str());
        ImGui::SameLine();
        drawAssetRow(entry.metadata, false);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace arti::editor
