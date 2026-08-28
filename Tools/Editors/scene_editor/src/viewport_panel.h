#pragma once
#include <cstdint>
#include <optional>
#include <utility>

namespace arti::rendering {
class Renderer;
} // namespace arti::rendering

namespace arti::editor {

// Viewport 面板：显示场景渲染结果。
class ViewportPanel {
public:
    explicit ViewportPanel(rendering::Renderer& renderer);

    // 绘制面板，返回当前面板尺寸 (width, height)
    std::pair<uint32_t, uint32_t> draw();

    // 焦点：点击后获得，用于键盘门禁。
    bool isFocused() const noexcept { return m_focused; }
    // 悬停：指针在面板上，用于鼠标门禁。不要拿 io.WantCaptureMouse 代替 —— 见 draw() 里的说明。
    bool isHovered() const noexcept { return m_hovered; }

    // 这一帧在场景图像上按下了左键时，返回图像内的像素坐标（左上原点）。
    //
    // 是「图像内」而不是窗口内：场景渲染目标的尺寸等于这个图像的尺寸，拾取要的是目标内的坐标。
    // 面板可能停靠在任意位置，所以必须减掉图像左上角的屏幕坐标。
    std::optional<std::pair<uint32_t, uint32_t>> consumeClick() noexcept {
        auto click = m_click;
        m_click.reset();
        return click;
    }

private:
    rendering::Renderer& m_renderer;
    bool m_focused{ false };
    bool m_hovered{ false };
    std::optional<std::pair<uint32_t, uint32_t>> m_click;
};

} // namespace arti::editor
