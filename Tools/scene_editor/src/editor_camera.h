#pragma once
#include "arti_renderer.h"

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace arti::editor {

class EditorCamera {
public:
    EditorCamera();

    void update(float deltaTime, bool mouse_owned, bool keyboard_owned);

    rendering::RenderView buildRenderView(uint32_t width, uint32_t height) const;

    void reset();

    glm::vec3 position() const noexcept { return m_position; }
    glm::quat orientation() const noexcept;

    void setPosition(const glm::vec3& position) noexcept { m_position = position; }
    void setOrientation(const glm::quat& orientation) noexcept;

    float moveSpeed() const noexcept { return m_move_speed; }
    void setMoveSpeed(float speed) noexcept { m_move_speed = speed; }

    float fov() const noexcept { return m_fov; }
    void setFov(float fov) noexcept { m_fov = fov; }

private:
    glm::vec3 m_position{ 0.0f, 2.0f, 5.0f };
    float m_yaw{ 0.0f };
    float m_pitch{ 0.0f };
    float m_fov{ 60.0f };
    float m_near_plane{ 0.1f };
    float m_far_plane{ 1000.0f };

    float m_move_speed{ 5.0f };
    float m_rotation_speed{ 0.003f };
    float m_sprint_multiplier{ 6.0f };
    float m_slow_multiplier{ 0.2f };
    float m_acceleration{ 12.0f };
    float m_speed_smoothing{ 8.0f };
    float m_look_smoothing{ 25.0f };
    float m_pan_speed{ 0.01f };
    float m_scroll_step{ 3.0f };

    float m_target_yaw{ 0.0f };
    float m_target_pitch{ 0.0f };
    float m_speed_multiplier{ 1.0f };
    glm::vec3 m_velocity{ 0.0f };

    bool m_active{ false };
};

} // namespace arti::editor
