#include "viewport_panel.h"

#include "arti_renderer.h"

#include <imgui.h>

namespace arti::editor {

ViewportPanel::ViewportPanel(rendering::Renderer& renderer)
        : m_renderer(renderer) {}

std::pair<uint32_t, uint32_t> ViewportPanel::draw() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    m_focused = ImGui::IsWindowFocused();
    // 相机的鼠标门禁用这个而不是 io.WantCaptureMouse：后者在指针悬于任何 ImGui 窗口时都为
    // true，而 Viewport 自己就是 ImGui 窗口，用它会导致相机永远拿不到鼠标。
    m_hovered = ImGui::IsWindowHovered();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const uint32_t width = available.x > 0.0f ? static_cast<uint32_t>(available.x) : 0;
    const uint32_t height = available.y > 0.0f ? static_cast<uint32_t>(available.y) : 0;

    if (width > 0 && height > 0) {
        // 图像左上角的屏幕坐标要在画之前取 —— 画完光标就移到下面去了。
        const ImVec2 image_origin = ImGui::GetCursorScreenPos();

        // SceneColor 是线性数据，ImGuiPass 认出这个 id 后会跳过 sRGB 解码
        ImGui::Image(m_renderer.sceneColorTextureId(),
                ImVec2{ static_cast<float>(width), static_cast<float>(height) });

        // 拾取用左键单击，而右键留给相机转视角。用 IsItemHovered 而不是窗口的悬停状态：
        // 点在面板的标题栏或者边框上不该触发拾取。
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            const float local_x = mouse.x - image_origin.x;
            const float local_y = mouse.y - image_origin.y;
            if (local_x >= 0.0f && local_y >= 0.0f && local_x < static_cast<float>(width) &&
                    local_y < static_cast<float>(height)) {
                m_click =
                        std::pair{ static_cast<uint32_t>(local_x), static_cast<uint32_t>(local_y) };
            }
        }
    }

    ImGui::End();

    return { width, height };
}

} // namespace arti::editor
