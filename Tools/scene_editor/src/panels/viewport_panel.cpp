#include "panels/viewport_panel.h"

#include "arti_renderer.h"
#include "editor_gizmo.h"

#include <imgui.h>

namespace arti::editor {

ViewportPanel::ViewportPanel(rendering::Renderer& renderer)
        : m_renderer(renderer) {}

std::pair<uint32_t, uint32_t> ViewportPanel::draw(
        const std::function<void(const ImageRect&)>& overlay) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    m_focused = ImGui::IsWindowFocused();
    m_hovered = ImGui::IsWindowHovered();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const uint32_t width = available.x > 0.0f ? static_cast<uint32_t>(available.x) : 0;
    const uint32_t height = available.y > 0.0f ? static_cast<uint32_t>(available.y) : 0;

    if (width > 0 && height > 0) {
        const ImVec2 image_origin = ImGui::GetCursorScreenPos();

        ImGui::Image(m_renderer.sceneColorTextureId(),
                ImVec2{ static_cast<float>(width), static_cast<float>(height) });

        m_image_rect = ImageRect{ image_origin.x, image_origin.y, static_cast<float>(width),
            static_cast<float>(height) };

        if (overlay) {
            overlay(m_image_rect);
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
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
