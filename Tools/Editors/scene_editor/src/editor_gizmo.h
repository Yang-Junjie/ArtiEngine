#pragma once
#include "arti_renderer.h"
#include "artichoco/core/uuid.h"

// ImGuizmo.h 用 ImVec2/ImU32 但不 include imgui.h。中间空行必需：合成一块后
// clang-format 会按字母序把 ImGuizmo.h 排到前面（大写 I < 小写 i），编译即失败。
#include <imgui.h>

#include <ImGuizmo.h>

#include <optional>

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::editor {

class EditorGizmo {
public:
    void handleShortcuts(bool enabled);

    void draw(scene::Scene& scene, const std::optional<core::UUID>& selected,
            const rendering::RenderView& view, float image_x, float image_y, float image_width,
            float image_height);

    bool isUsing() const noexcept { return m_using; }

    ImGuizmo::OPERATION operation() const noexcept { return m_operation; }
    ImGuizmo::MODE mode() const noexcept { return m_mode; }
    void setMode(ImGuizmo::MODE mode) noexcept { m_mode = mode; }

private:
    ImGuizmo::OPERATION m_operation{ ImGuizmo::TRANSLATE };
    ImGuizmo::MODE m_mode{ ImGuizmo::LOCAL };
    bool m_using{ false };
};

} // namespace arti::editor
