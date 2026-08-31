#include "editor_camera.h"

#include "artichoco/core/io/input.h"
#include "artichoco/core/io/key_codes.h"
#include "artichoco/core/io/mouse_codes.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace arti::editor {
namespace {

constexpr float kMaxPitch = glm::half_pi<float>() - 0.01f;
constexpr float kMaxDeltaTime = 0.1f;

// 帧率无关的指数趋近系数：k = 1 - exp(-rate * dt)。
// rate 越大趋近越快，rate=0 不变化；dt 越大单帧趋近比例越高。
float smoothFactor(float rate, float deltaTime) {
    return 1.0f - std::exp(-rate * deltaTime);
}

} // namespace

EditorCamera::EditorCamera() { reset(); }

void EditorCamera::update(float deltaTime, bool mouse_owned, bool keyboard_owned) {
    // 窗口失焦或卡顿造成的超大 dt 会让相机瞬移，先钳制。
    deltaTime = std::min(deltaTime, kMaxDeltaTime);

    // Esc 随时退出飞行模式（键盘被 ImGui 占用的场景不抢键）。
    if (keyboard_owned && core::Input::isKeyPressed(core::KeyCode::Escape)) {
        m_active = false;
    }

    const bool right_button = core::Input::isMouseButtonPressed(core::MouseCode::Right);
    const bool middle_button = core::Input::isMouseButtonPressed(core::MouseCode::Middle);
    const bool flying = right_button && (mouse_owned || m_active);
    const bool panning = middle_button && (mouse_owned || m_active);

    if (!flying) {
        m_active = false;
    }

    const glm::vec2 mouse_delta = core::Input::getMouseDelta();

    if (flying) {
        if (!m_active) {
            // 进入飞行：从当前朝向出发，避免"跳一下"。
            m_active = true;
            m_target_yaw = m_yaw;
            m_target_pitch = m_pitch;
        }

        // 视角：delta 累计到 target，再指数趋近。转动带一点顺滑的惯性，
        // 又不会像直接平滑 delta 那样丢手。pitch 钳在 ±89° 防止翻转。
        m_target_yaw -= mouse_delta.x * m_rotation_speed;
        m_target_pitch -= mouse_delta.y * m_rotation_speed;
        m_target_pitch = glm::clamp(m_target_pitch, -kMaxPitch, kMaxPitch);
        const float look_k = smoothFactor(m_look_smoothing, deltaTime);
        m_yaw = glm::mix(m_yaw, m_target_yaw, look_k);
        m_pitch = glm::mix(m_pitch, m_target_pitch, look_k);
    }

    if (panning) {
        // 中键平移：在相机平面内移动，量随速度档位放大，拖起来"跟手"。
        const glm::quat orientation = this->orientation();
        const glm::vec3 right = orientation * glm::vec3{ 1.0f, 0.0f, 0.0f };
        const glm::vec3 up = -orientation * glm::vec3{ 0.0f, 1.0f, 0.0f };
        m_position -= (right * mouse_delta.x + up * mouse_delta.y) * m_pan_speed *
                      std::max(m_speed_multiplier, 1.0f);
    }

    // 速度档位：Shift 冲刺 / Ctrl 慢速，切换是平滑的，不会"跳档"。
    float target_multiplier = 1.0f;
    if (core::Input::isKeyPressed(core::KeyCode::LeftShift) ||
            core::Input::isKeyPressed(core::KeyCode::RightShift)) {
        target_multiplier = m_sprint_multiplier;
    } else if (core::Input::isKeyPressed(core::KeyCode::LeftControl) ||
               core::Input::isKeyPressed(core::KeyCode::RightControl)) {
        target_multiplier = m_slow_multiplier;
    }
    m_speed_multiplier =
            glm::mix(m_speed_multiplier, target_multiplier, smoothFactor(m_speed_smoothing, deltaTime));

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

    glm::vec3 target_velocity{ 0.0f };
    if (flying && keyboard_owned && glm::length(input) > 0.0f) {
        input = glm::normalize(input);
        const glm::quat orientation = this->orientation();
        const glm::vec3 forward = orientation * glm::vec3{ 0.0f, 0.0f, -1.0f };
        const glm::vec3 right = orientation * glm::vec3{ 1.0f, 0.0f, 0.0f };
        constexpr glm::vec3 world_up{ 0.0f, 1.0f, 0.0f };

        target_velocity =
                (forward * -input.z + right * input.x + world_up * input.y) * m_move_speed *
                m_speed_multiplier;
    }

    // 速度指数趋近目标：起步有加速感，松键平滑刹停，且帧率无关。
    m_velocity = glm::mix(m_velocity, target_velocity, smoothFactor(m_acceleration, deltaTime));
    m_position += m_velocity * deltaTime;

    // 滚轮沿视线方向推拉（dolly），不要求按住右键。
    const float scroll_y = core::Input::getMouseScrollOffset().y;
    if (scroll_y != 0.0f && (mouse_owned || m_active)) {
        const glm::vec3 forward = orientation() * glm::vec3{ 0.0f, 0.0f, -1.0f };
        m_position += forward * scroll_y * m_scroll_step * std::max(m_speed_multiplier, 1.0f);
    }
}

rendering::RenderView EditorCamera::buildRenderView(uint32_t width, uint32_t height) const {
    rendering::RenderView view;
    if (width == 0 || height == 0) {
        return view;
    }

    const glm::mat4 world =
            glm::translate(glm::mat4{ 1.0f }, m_position) * glm::mat4_cast(orientation());
    view.view = glm::inverse(world);

    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    view.projection = glm::perspectiveRH_ZO(glm::radians(m_fov), aspect, m_near_plane, m_far_plane);
    view.camera_position = m_position;

    return view;
}

glm::quat EditorCamera::orientation() const noexcept {
    const glm::quat yaw = glm::angleAxis(m_yaw, glm::vec3{ 0.0f, 1.0f, 0.0f });
    const glm::quat pitch = glm::angleAxis(m_pitch, glm::vec3{ 1.0f, 0.0f, 0.0f });
    return glm::normalize(yaw * pitch);
}

void EditorCamera::setOrientation(const glm::quat& orientation) noexcept {
    const glm::vec3 forward = glm::normalize(orientation * glm::vec3{ 0.0f, 0.0f, -1.0f });
    m_yaw = std::atan2(-forward.x, -forward.z);
    m_pitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
    m_target_yaw = m_yaw;
    m_target_pitch = m_pitch;
}

void EditorCamera::reset() {
    m_position = glm::vec3{ 0.0f, 2.0f, 5.0f };

    const glm::vec3 forward = glm::normalize(glm::vec3{ 0.0f } - m_position);
    m_yaw = std::atan2(-forward.x, -forward.z);
    m_pitch = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
    m_target_yaw = m_yaw;
    m_target_pitch = m_pitch;

    m_fov = 60.0f;
    m_move_speed = 5.0f;
    m_speed_multiplier = 1.0f;
    m_velocity = glm::vec3{ 0.0f };
    m_active = false;
}

} // namespace arti::editor
