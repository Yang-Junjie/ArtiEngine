#include "editor_gizmo.h"

#include "artichoco/scene/components.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace arti::editor {
namespace {

void decomposeAffine(const glm::mat4& matrix, glm::vec3& translation, glm::quat& rotation,
        glm::vec3& scale) {
    translation = glm::vec3{ matrix[3] };

    glm::mat3 basis{ matrix };
    scale = glm::vec3{ glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2]) };

    for (int axis = 0; axis < 3; ++axis) {
        // 缩放为 0 的轴归一化会除零，NaN 写进组件后那个实体就再也画不出来了。
        if (scale[axis] > 0.0f) {
            basis[axis] /= scale[axis];
        } else {
            basis[axis] = glm::mat3{ 1.0f }[axis];
        }
    }
    rotation = glm::normalize(glm::quat_cast(basis));
}

} // namespace

void EditorGizmo::handleShortcuts(bool enabled) {
    if (!enabled) {
        return;
    }

    // 这三个是光秃秃的字母键，没有修饰键给它们兜底，所以文本框活跃时必须让路 ——
    // 否则在 Inspector 里给实体改名，打一个 "w" 就顺手把手柄切成了 translate。
    // 编辑器相机的 WASD 走 core::Input，和这里是两条路，不受影响。
    if (ImGui::GetIO().WantTextInput) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_W, false) ) {
        m_operation = ImGuizmo::TRANSLATE;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E, false) ) {
        m_operation = ImGuizmo::ROTATE;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_R, false) ) {
        m_operation = ImGuizmo::SCALE;
    }
}

void EditorGizmo::draw(scene::Scene& scene, const std::optional<core::UUID>& selected,
        const rendering::RenderView& view, float image_x, float image_y, float image_width,
        float image_height) {
    m_using = false;
    if (!selected.has_value() || image_width <= 0.0f || image_height <= 0.0f) {
        return;
    }

    auto entity = scene.findEntity(*selected);
    if (!entity.isValid()) {
        return;
    }

    const glm::mat4 parent_world = [&]() {
        const auto& parent = entity.getComponent<scene::ParentComponent>();
        if (!parent.parent_id.isValid()) {
            return glm::mat4{ 1.0f };
        }
        auto parent_entity = scene.findEntity(parent.parent_id);
        return parent_entity.isValid() ? scene.getWorldTransform(parent_entity) : glm::mat4{ 1.0f };
    }();

    glm::mat4 world = scene.getWorldTransform(entity);

    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(image_x, image_y, image_width, image_height);

    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(view.view),
            glm::value_ptr(view.projection), m_operation, m_mode, glm::value_ptr(world));
    m_using = ImGuizmo::IsUsing();

    if (!changed) {
        return;
    }

    const glm::mat4 local = glm::affineInverse(parent_world) * world;

    auto& transform = entity.getComponent<scene::TransformComponent>();
    decomposeAffine(local, transform.translation, transform.rotation, transform.scale);
}

} // namespace arti::editor
