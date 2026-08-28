#include "editor_camera.h"

#include "artichoco/core/io/input.h"
#include "artichoco/core/io/key_codes.h"
#include "artichoco/core/io/mouse_codes.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace arti::editor {

EditorCamera::EditorCamera() { reset(); }

void EditorCamera::update(float deltaTime, bool mouse_owned, bool keyboard_owned) {
    const bool right_button = core::Input::isMouseButtonPressed(core::MouseCode::Right);
    const glm::vec2 mouse = core::Input::getMousePosition();

    const bool flying = right_button && (mouse_owned || m_active);
    if (!flying) {
        m_active = false;
        return;
    }

    if (!m_active) {
        m_active = true;
        m_last_mouse_x = mouse.x;
        m_last_mouse_y = mouse.y;
    } else {
        const float delta_x = mouse.x - m_last_mouse_x;
        const float delta_y = mouse.y - m_last_mouse_y;
        m_last_mouse_x = mouse.x;
        m_last_mouse_y = mouse.y;

        // 不乘 deltaTime：鼠标 delta 是位移量不是速率，乘了等于按帧率缩放灵敏度。
        const float yaw_delta = -delta_x * m_rotation_speed;
        const float pitch_delta = -delta_y * m_rotation_speed;

        const glm::quat yaw{ glm::angleAxis(yaw_delta, glm::vec3{ 0.0f, 1.0f, 0.0f }) };
        const glm::quat pitch{ glm::angleAxis(pitch_delta, glm::vec3{ 1.0f, 0.0f, 0.0f }) };

        m_orientation = glm::normalize(yaw * m_orientation * pitch);
    }

    if (!keyboard_owned) {
        return;
    }

    glm::vec3 input{ 0.0f };
    if (core::Input::isKeyPressed(core::KeyCode::W)) {
        input.z -= 1.0f;
    }
    if (core::Input::isKeyPressed(core::KeyCode::S)) {
        input.z += 1.0f;
    }
    if (core::Input::isKeyPressed(core::KeyCode::A)) {
        input.x -= 1.0f;
    }
    if (core::Input::isKeyPressed(core::KeyCode::D)) {
        input.x += 1.0f;
    }
    if (core::Input::isKeyPressed(core::KeyCode::Q)) {
        input.y -= 1.0f;
    }
    if (core::Input::isKeyPressed(core::KeyCode::E)) {
        input.y += 1.0f;
    }

    if (glm::length(input) > 0.0f) {
        input = glm::normalize(input);
        const glm::vec3 forward = m_orientation * glm::vec3{ 0.0f, 0.0f, -1.0f };
        const glm::vec3 right = m_orientation * glm::vec3{ 1.0f, 0.0f, 0.0f };
        constexpr glm::vec3 world_up{ 0.0f, 1.0f, 0.0f };

        m_position += (forward * -input.z + right * input.x + world_up * input.y) * m_move_speed *
                      deltaTime;
    }
}

rendering::RenderView EditorCamera::buildRenderView(uint32_t width, uint32_t height) const {
    rendering::RenderView view;
    if (width == 0 || height == 0) {
        return view;
    }

    const glm::mat4 world =
            glm::translate(glm::mat4{ 1.0f }, m_position) * glm::mat4_cast(m_orientation);
    view.view = glm::inverse(world);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    view.projection = glm::perspectiveRH_ZO(glm::radians(m_fov), aspect, m_near_plane, m_far_plane);
    view.camera_position = m_position;

    return view;
}

void EditorCamera::reset() {
    m_position = glm::vec3{ 0.0f, 2.0f, 5.0f };
    const glm::mat4 world =
            glm::inverse(glm::lookAt(m_position, glm::vec3{ 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f }));
    m_orientation = glm::quat_cast(world);
    m_fov = 60.0f;
    m_move_speed = 5.0f;
    m_active = false;
}

} // namespace arti::editor
