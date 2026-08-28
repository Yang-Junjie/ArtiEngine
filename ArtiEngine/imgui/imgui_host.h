#pragma once
#include "arti_renderer.h"

#include <cstdint>

struct ImDrawData;
struct ImGuiContext;

namespace arti::core {
class Window;
} // namespace arti::core

namespace arti::platform {
class SDLWindow;
} // namespace arti::platform

namespace arti::engine {

struct ImGuiHostCreateInfo {
    bool docking{ true };
    bool persist_layout{ true };
};

class ImGuiHost {
public:
    ImGuiHost(core::Window& window, rendering::Renderer& renderer,
            const ImGuiHostCreateInfo& create_info = {});
    ~ImGuiHost();

    ImGuiHost(const ImGuiHost&) = delete;
    ImGuiHost& operator=(const ImGuiHost&) = delete;

    void beginFrame();
    void endFrame();

    void dockSpaceOverViewport();

    [[nodiscard]] bool isDockingEnabled() const noexcept { return m_docking; }

    [[nodiscard]] rendering::FrameOverlay overlay() const noexcept;

    [[nodiscard]] bool wantsMouseInput() const noexcept;
    [[nodiscard]] bool wantsKeyboardInput() const noexcept;

    [[nodiscard]] bool wantsTextInput() const noexcept;

private:
    void createFontTexture();

    platform::SDLWindow& m_window;
    rendering::Renderer& m_renderer;
    ImGuiContext* m_context{ nullptr };
    ImDrawData* m_draw_data{ nullptr };
    uint64_t m_event_observer_id{ 0 };
    bool m_frame_started{ false };
    bool m_docking{ false };
    rendering::TextureHandle m_font_texture;
};

} // namespace arti::engine
