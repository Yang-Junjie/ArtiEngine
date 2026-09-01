#pragma once
#include <cstddef>

namespace arti::editor {

class SceneDocument;

// 项目设置对话框。目前只有一件事：设 StartScene —— 独立运行时（arti_player）启动时读的
// 就是它，在这之前这个字段没有任何界面能写。
//
// 做成模态而不是常驻面板：一个项目从头到尾只需要设一次，摆在停靠布局里是纯占地方。
class ProjectSettingsPanel {
public:
    explicit ProjectSettingsPanel(SceneDocument& document);

    // 请求打开对话框（下一次 draw 生效），并把编辑缓冲刷成项目里的当前值。
    void open();

    // 每帧调。没有打开请求也没在显示时是空操作。
    void draw();

private:
    void applyToProject();

    SceneDocument* m_document{ nullptr };
    bool m_open_requested{ false };

    // 编辑缓冲：改动只在按 Save 时落进 ProjectInfo，Cancel 就整个丢掉。
    // 用定长缓冲是因为 ImGui::InputText 要的就是它 —— 这份 imgui 没编 misc/cpp 里的
    // std::string 适配层。
    static constexpr std::size_t kPathBufferSize = 512;
    char m_start_scene[kPathBufferSize]{};
};

} // namespace arti::editor
