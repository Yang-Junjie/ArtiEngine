#pragma once
#include "arti_renderer.h"

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace arti::editor {

// 编辑器相机控制器。不是场景里的实体，只是个独立的视角控制 + RenderView 生成器。
//
// - WASD / QE 平移
// - 鼠标右键拖拽旋转视角
// - 鼠标滚轮缩放移动速度
//
// PlayMode 时被禁用，场景里的 CameraComponent 接管。
class EditorCamera {
public:
    EditorCamera();

    // 每帧调一次。deltaTime 单位是秒。
    //
    // 鼠标和键盘的归属分开传，因为判据不同：鼠标看指针是否在 Viewport 上，键盘看焦点是否
    // 落在某个输入框里（在 Inspector 里打字不该让相机飞走）。由调用方决定，相机不认识 ImGui。
    void update(float deltaTime, bool mouse_owned, bool keyboard_owned);

    // 生成当前视角的 RenderView。EditorMode 时用这个，PlayMode 时用场景相机。
    rendering::RenderView buildRenderView(uint32_t width, uint32_t height) const;

    // 重置到初始位置和朝向。
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
    glm::quat m_orientation{ 1.0f, 0.0f, 0.0f, 0.0f }; // identity
    float m_fov{ 60.0f };
    float m_near_plane{ 0.1f };
    float m_far_plane{ 1000.0f };
    float m_move_speed{ 5.0f };
    // 弧度 / 像素。不含 dt —— 见 update() 里的说明。0.003 大致是「拖过屏幕宽度转小半圈」，
    // 和常见编辑器的手感接近。
    float m_rotation_speed{ 0.003f };

    // 右键拖拽状态
    bool m_rotating{ false };
    float m_last_mouse_x{ 0.0f };
    float m_last_mouse_y{ 0.0f };
};

} // namespace arti::editor
