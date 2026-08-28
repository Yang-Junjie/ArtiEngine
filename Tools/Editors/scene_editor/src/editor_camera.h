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
    glm::quat orientation() const noexcept { return m_orientation; }

    void setPosition(const glm::vec3& position) noexcept { m_position = position; }
    void setOrientation(const glm::quat& orientation) noexcept { m_orientation = orientation; }

    float moveSpeed() const noexcept { return m_move_speed; }
    void setMoveSpeed(float speed) noexcept { m_move_speed = speed; }

    float fov() const noexcept { return m_fov; }
    void setFov(float fov) noexcept { m_fov = fov; }

private:
    glm::vec3 m_position{ 0.0f, 2.0f, 5.0f };
    glm::quat m_orientation{ 1.0f, 0.0f, 0.0f, 0.0f };
    float m_fov{ 60.0f };
    float m_near_plane{ 0.1f };
    float m_far_plane{ 1000.0f };
    float m_move_speed{ 5.0f };
    float m_rotation_speed{ 0.003f };

    bool m_active{ false };
    float m_last_mouse_x{ 0.0f };
    float m_last_mouse_y{ 0.0f };
};

} // namespace arti::editor
