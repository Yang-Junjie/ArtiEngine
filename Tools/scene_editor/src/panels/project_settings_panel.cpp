#include "panels/project_settings_panel.h"

#include "editor_paths.h"
#include "scene_document.h"

#include "panels/ui_widgets.h"
#include "platform/common/file_dialogs.h"

#include "artichoco/core/application.h"
#include "artichoco/project/project_manager.h"

#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>

namespace arti::editor {
namespace {

constexpr const char* kPopupTitle = "Project Settings";
constexpr const char* kSceneFilter = "Arti Scene\0*.artiscene\0";

const core::Logger::Channel& log() { return core::Application::get().getLogChannel(); }

void copyInto(char* buffer, std::size_t size, const std::string& text) {
    const std::size_t length = std::min(text.size(), size - 1);
    std::memcpy(buffer, text.data(), length);
    buffer[length] = '\0';
}

// StartScene 存的是项目根相对路径，所以校验也在项目根下做。
bool sceneExists(const char* relative) {
    const auto root = project::ProjectManager::instance().getProjectRootPath();
    if (!root || relative[0] == '\0') {
        return false;
    }
    std::error_code error;
    return std::filesystem::is_regular_file(*root / std::filesystem::path{ relative }, error) &&
           !error;
}

} // namespace

ProjectSettingsPanel::ProjectSettingsPanel(SceneDocument& document)
        : m_document(&document) {}

void ProjectSettingsPanel::open() {
    const auto& info = project::ProjectManager::instance().getProjectInfo();
    copyInto(m_start_scene, kPathBufferSize,
            info ? info->start_scene.generic_string() : std::string{});
    m_open_requested = true;
}

void ProjectSettingsPanel::draw() {
    if (m_open_requested) {
        ImGui::OpenPopup(kPopupTitle);
        m_open_requested = false;
    }

    // 居中：模态是个临时窗口，让它出现在鼠标上次点菜单的地方会很跳。
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2{ 0.5f, 0.5f });
    ImGui::SetNextWindowSize(ImVec2{ 520.0f, 0.0f }, ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal(kPopupTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    auto& projects = project::ProjectManager::instance();
    const auto& info = projects.getProjectInfo();
    const auto root = projects.getProjectRootPath();
    if (!info || !root) {
        // 理论上到不了：菜单项在没开项目时是灰的。留着是因为项目可以在对话框开着的时候被换掉。
        ImGui::TextUnformatted("No project is open.");
        if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    if (beginPropertyGrid("##project_settings_grid", 120.0f)) {
        drawTextRow("Name", info->name.c_str());
        drawTextRow("Root", root->generic_string().c_str(), "项目根目录。StartScene 是相对它的。");
        endPropertyGrid();
    }

    ImGui::Spacing();
    if (beginSection("Start Scene")) {
        ImGui::TextWrapped("arti_player 启动时加载的场景。路径相对项目根。");
        ImGui::Spacing();

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputTextWithHint("##start_scene", "Assets/Scenes/Main.artiscene", m_start_scene,
                kPathBufferSize);

        // 当前正在编辑的场景。没存过盘（m_file 为空）时没有路径可用，所以按钮是灰的。
        const bool has_current = !m_document->file().empty();
        if (!has_current) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Use Current Scene")) {
            if (const auto relative = relativeToProjectRoot(m_document->file())) {
                copyInto(m_start_scene, kPathBufferSize, relative->generic_string());
            } else {
                log().warn("The current scene is outside the project, so it cannot be the "
                           "start scene");
            }
        }
        if (!has_current) {
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("当前场景还没有保存过，没有路径可以填。");
        }

        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            const auto file = FileDialogs::openFile(kSceneFilter, root->string());
            if (!file.empty()) {
                if (const auto relative = relativeToProjectRoot(file)) {
                    copyInto(m_start_scene, kPathBufferSize, relative->generic_string());
                } else {
                    log().warn("'{}' is outside the project, so it cannot be the start scene",
                            file.string());
                }
            }
        }

        // 只是提示，不阻止保存：场景文件可能还没建出来，先把名字定下来是合理的。
        if (m_start_scene[0] == '\0') {
            ImGui::TextColored(ImVec4{ 1.0f, 0.7f, 0.3f, 1.0f },
                    "未设置 —— arti_player 会因为没有起始场景而退出。");
        } else if (!sceneExists(m_start_scene)) {
            ImGui::TextColored(ImVec4{ 1.0f, 0.4f, 0.3f, 1.0f }, "这个文件当前不存在。");
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Save", ImVec2{ 100.0f, 0.0f })) {
        applyToProject();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2{ 100.0f, 0.0f })) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void ProjectSettingsPanel::applyToProject() {
    auto& projects = project::ProjectManager::instance();
    const auto& info = projects.getProjectInfo();
    if (!info) {
        return;
    }

    project::ProjectInfo updated = *info;
    updated.start_scene = std::filesystem::path{ m_start_scene }.lexically_normal();

    // setProjectInfo 对绝对路径和带 .. 的路径直接抛 —— 输入框里用户什么都能敲进来，
    // 所以这里必须接住，否则手抖一下整个编辑器就没了。
    try {
        projects.setProjectInfo(updated);
    } catch (const std::exception& exception) {
        log().error("Rejected the start scene '{}': {}", m_start_scene, exception.what());
        return;
    }
    if (!projects.saveProject()) {
        log().error("Failed to write the project file");
        return;
    }
    log().info("Start scene set to '{}'", updated.start_scene.generic_string());
}

} // namespace arti::editor
