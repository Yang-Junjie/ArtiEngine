#pragma once
#include "arti_renderer.h"

#include <cstdint>
#include <filesystem>

struct ImDrawData;
struct ImGuiContext;

namespace arti::core {
class Window;
}

namespace arti::platform {
class SDLWindow;
}

namespace arti::engine {

struct ImGuiHostCreateInfo {
    bool docking{ true };
    bool persist_layout{ true };

    // UI 字体的 TTF / OTF 路径。空的话用 ImGui 内建的位图字体。
    //
    // 加载失败不会抛 —— 退回内建字体并记一条 warn。缺一个字体不该让整个编辑器起不来，
    // 而字体是从源码树读的（见调用方），换台机器最容易缺的就是它。
    std::filesystem::path font_path;
    // 光栅化尺寸。内建位图字体是 13px 且只在这个尺寸下清晰；矢量字体给 16 更好读。
    float font_size_pixels{ 16.0f };
    // 要不要连简体中文常用字一起烘。开着的代价是图集变大、启动多花几十到几百毫秒，
    // 换来的是实体名、资产路径、面板文字里的中文不再显示成方框。详见 imgui_host.cpp。
    bool chinese_glyphs{ true };
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
    void loadFont(const ImGuiHostCreateInfo& create_info);
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

}
