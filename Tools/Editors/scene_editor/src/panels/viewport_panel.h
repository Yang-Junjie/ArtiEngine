#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace arti::rendering {
class Renderer;
} // namespace arti::rendering

namespace arti::editor {

class ViewportPanel {
public:
    explicit ViewportPanel(rendering::Renderer& renderer);

    struct ImageRect {
        float x{ 0.0f };
        float y{ 0.0f };
        float width{ 0.0f };
        float height{ 0.0f };
    };

    std::pair<uint32_t, uint32_t> draw(const std::function<void(const ImageRect&)>& overlay = {});

    const ImageRect& imageRect() const noexcept { return m_image_rect; }

    bool isFocused() const noexcept { return m_focused; }
    bool isHovered() const noexcept { return m_hovered; }

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
    ImageRect m_image_rect;
};

} // namespace arti::editor
