#include "editor_camera.h"

#include "artichoco/core/io/input.h"
#include "artichoco/core/io/key_codes.h"
#include "artichoco/core/io/mouse_codes.h"

// mat4_cast 在 gtc 里，不用 gtx/quaternion.hpp —— 那个是 experimental，
// 要额外 define GLM_ENABLE_EXPERIMENTAL 才能用。
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace arti::editor {

EditorCamera::EditorCamera() { reset(); }

void EditorCamera::update(float deltaTime, bool mouse_owned, bool keyboard_owned) {
    // 右键拖拽转视角。按下的那一刻必须指针在 Viewport 上；一旦开始拖，就算指针滑出面板
    // 也继续跟随（松开右键才结束）—— 否则快速转视角到边缘会突然断掉。
    const bool right_button = core::Input::isMouseButtonPressed(core::MouseCode::Right);
    const glm::vec2 mouse = core::Input::getMousePosition();

    if (right_button && (mouse_owned || m_rotating)) {
        if (!m_rotating) {
            m_rotating = true;
            m_last_mouse_x = mouse.x;
            m_last_mouse_y = mouse.y;
        } else {
            const float delta_x = mouse.x - m_last_mouse_x;
            const float delta_y = mouse.y - m_last_mouse_y;
            m_last_mouse_x = mouse.x;
            m_last_mouse_y = mouse.y;

            // 刻意**不乘** deltaTime：鼠标 delta 已经是「这一帧移动了多少像素」，是位移量
            // 不是速率。再乘一次 dt 等于按帧率缩放灵敏度（60fps 下砍到 1/60，看起来就是不动）。
            // 下面 WASD 乘 dt 是对的，那个是速度。
            const float yaw_delta = -delta_x * m_rotation_speed;
            const float pitch_delta = -delta_y * m_rotation_speed;

            // yaw 绕世界 Y 轴（左乘），pitch 绕相机局部 X 轴（右乘）。
            // 这个顺序保证视角不会滚转（roll）—— 和大多数编辑器的飞行相机一致。
            const glm::quat yaw{ glm::angleAxis(yaw_delta, glm::vec3{ 0.0f, 1.0f, 0.0f }) };
            const glm::quat pitch{ glm::angleAxis(pitch_delta, glm::vec3{ 1.0f, 0.0f, 0.0f }) };

            m_orientation = glm::normalize(yaw * m_orientation * pitch);
        }
    } else {
        m_rotating = false;
    }

    if (!keyboard_owned) {
        return;
    }

    // WASD 前后左右、QE 升降
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
        // 升降用世界 Y 轴而不是相机局部的 up：俯视时按 E 应该真的往上走，
        // 而不是朝着相机背后飘。
        constexpr glm::vec3 world_up{ 0.0f, 1.0f, 0.0f };

        m_position += (forward * -input.z + right * input.x + world_up * input.y) * m_move_speed *
                      deltaTime;
    }
}

rendering::RenderView EditorCamera::buildRenderView(uint32_t width, uint32_t height) const {
    rendering::RenderView view;
    // 尺寸退化时返回单位矩阵，调用方应该整帧跳过。
    if (width == 0 || height == 0) {
        return view;
    }

    // view 是相机世界变换的逆。
    const glm::mat4 world =
            glm::translate(glm::mat4{ 1.0f }, m_position) * glm::mat4_cast(m_orientation);
    view.view = glm::inverse(world);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    // RH_ZO：深度范围 [0,1]，和 ArtiRenderer 的 pass 约定一致。
    view.projection = glm::perspectiveRH_ZO(glm::radians(m_fov), aspect, m_near_plane, m_far_plane);
    view.camera_position = m_position;

    return view;
}

void EditorCamera::reset() {
    m_position = glm::vec3{ 0.0f, 2.0f, 5.0f };
    // 朝向原点而不是单位四元数（正对 -Z、无俯角）。相机在 y=2，物体通常在 y=0 附近，
    // 不带俯角的话打开编辑器屏幕正中是空的，物体在下半屏。
    //
    // lookAt 给的是 view 矩阵，取逆才是相机的世界变换，再转成四元数。
    const glm::mat4 world =
            glm::inverse(glm::lookAt(m_position, glm::vec3{ 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f }));
    m_orientation = glm::quat_cast(world);
    m_fov = 60.0f;
    m_move_speed = 5.0f;
    m_rotating = false;
}

} // namespace arti::editor
