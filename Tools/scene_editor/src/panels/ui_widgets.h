#pragma once
#include "artichoco/core/uuid.h"

#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>

// 编辑器各面板共用的 ImGui 小部件。Inspector 和 Content Browser 的属性区必须长得
// 一样，所以这套东西放在一个头里，而不是各自复制一份。全 inline，不需要 .cpp。
//
// 面板专用的控件留在各自的 .cpp 里：带 glm 的向量/颜色行、按组件类型模板化的头部 ——
// 挪进来会把 glm 和 scene 的头拖给每一个引用方。
namespace arti::editor {

// 主题强调色，和 ImGuiHost::setDarkThemeColors() 里的橙色是同一个值。
inline constexpr ImVec4 kAccentColor{ 0.92f, 0.45f, 0.11f, 1.00f };

// UUID 文本形式的长度（16 位十六进制）。
inline constexpr std::size_t kUuidTextLength = 16;

// ---- 属性网格（两列：固定宽标签列 + 拉伸值列，交替行底色）----

// id 同时是列宽的持久化身份：同 id 的网格共享用户拖出来的列宽。Inspector 各组件
// 故意都用默认值，这样所有组件的标签列对齐；别的面板传自己的 id 和列宽。
//
// 返回 false 表示表格没开起来，此时样式已经弹回，调用方必须跳过整个网格 ——
// 继续调 propertyRow() 会解引用空表指针。窗口折叠时 BeginTable 就会这样，所以
// 每个面板的 draw() 都该先写 `if (!ImGui::Begin(...)) { ImGui::End(); return; }`：
// 有了那道守卫，这里就不会失败。
inline bool beginPropertyGrid(const char* id = "##property_grid", float label_width = 110.0f) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2{ 6.0f, 3.5f });
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4{ 0.0f, 0.0f, 0.0f, 0.05f });
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4{ 1.0f, 1.0f, 1.0f, 0.02f });
    if (!ImGui::BeginTable(id, 2,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                        ImGuiTableFlags_SizingFixedFit)) {
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        return false;
    }
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, label_width);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

inline void endPropertyGrid() {
    ImGui::EndTable();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

// 起一行并把光标留在值列，接下来画什么控件由调用方决定。
//
// 注意：这个函数不动 ID 栈。值列里画的控件如果用固定字面量当 ID（"##value" 这种），
// 同一个网格里出现两次就撞车了 —— ImGui 的 ActiveId 是全局唯一的一个值，凡是 id
// 等于它的 item 都会跑一遍交互逻辑（见 imgui_widgets.cpp 的 DragBehavior：
// `if (g.ActiveId != id) return false;` 之后就直接改数据），所以拖一个框会把同 ID
// 的其他框一起拖动。调用方要么给每个控件不同的 ID，要么在外面 PushID(label)。
// 下面自带控件的那几个 draw*Row 已经在内部 PushID 了。
inline void propertyRow(const char* label, const char* tooltip = nullptr) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (tooltip != nullptr) {
        ImGui::SetItemTooltip("%s", tooltip);
    }
    ImGui::TableSetColumnIndex(1);
}

inline void drawBoolRow(const char* label, bool* value, const char* tooltip = nullptr) {
    propertyRow(label, tooltip);
    // 用标签当 ID 作用域，否则一个网格里两行的 "##value" 是同一个 ID。
    // 标签本身在同一个网格里重复的话（两行都叫 Enabled），调用方还得自己 PushID。
    ImGui::PushID(label);
    ImGui::Checkbox("##value", value);
    ImGui::PopID();
}

inline void drawFloatRow(const char* label, float* value, float speed, float min, float max,
        const char* format = "%.3f", const char* tooltip = nullptr) {
    propertyRow(label, tooltip);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::DragFloat("##value", value, speed, min, max, format);
    ImGui::PopID();
}

// 只读文本行。值按单元格宽度折行而不是截断 —— 路径和 UUID 截断了就没用了。
inline void drawTextRow(const char* label, const char* value, const char* tooltip = nullptr) {
    propertyRow(label, tooltip);
    ImGui::TextWrapped("%s", value);
}

inline void drawTextRowColored(const char* label, const ImVec4& color, const char* value,
        const char* tooltip = nullptr) {
    propertyRow(label, tooltip);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", value);
    ImGui::PopStyleColor();
}

// ---- 分节 / 按钮 / 输入 / 空状态 ----

// CollapsingHeader，悬停时给一点主题色底暗示可交互。只有箭头能展开收起，
// 这样点标题栏其他地方不会误触。返回 false 表示这一节是收起的。
inline bool beginSection(const char* label, bool default_open = true) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (default_open) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{ 0.92f, 0.45f, 0.11f, 0.12f });
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{ 0.92f, 0.45f, 0.11f, 0.22f });
    const bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleColor(2);
    return open;
}

// 整宽暗色按钮，用于 "+ Add Component" / "+ Add Material" 这类一行一个的动作。
inline bool fullWidthDimButton(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.16f, 0.16f, 0.17f, 0.60f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.92f, 0.45f, 0.11f, 0.22f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.92f, 0.45f, 0.11f, 0.40f });
    const bool clicked = ImGui::Button(label, ImVec2{ -1.0f, 0.0f });
    ImGui::PopStyleColor(3);
    return clicked;
}

// UUID 输入框。text 跨帧保留用户敲到一半的内容，所以由调用方持有；解析成功时
// 写进 applied 并返回 true。
inline bool drawUuidInput(const char* label, std::string& text, core::UUID& applied) {
    char buffer[kUuidTextLength + 1]{};
    std::memcpy(buffer, text.data(), std::min(text.size(), kUuidTextLength));

    if (ImGui::InputText(label, buffer, sizeof(buffer),
                ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_AutoSelectAll)) {
        text.assign(buffer);
    }
    if (const auto parsed = core::UUID::fromString(text)) {
        applied = *parsed;
        return true;
    }
    return false;
}

// 面板或子窗口中央的一行灰字，用于「没打开项目」「没选中东西」这类空状态。
// 用可用区域而不是窗口尺寸来算，所以在子窗口里也居中正确。
inline void drawEmptyState(const char* message) {
    const ImVec2 region = ImGui::GetContentRegionAvail();
    const ImVec2 text_size = ImGui::CalcTextSize(message);
    const ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2{ cursor.x + (region.x - text_size.x) * 0.5f,
        cursor.y + (region.y - text_size.y) * 0.5f });
    ImGui::TextDisabled("%s", message);
}

} // namespace arti::editor
