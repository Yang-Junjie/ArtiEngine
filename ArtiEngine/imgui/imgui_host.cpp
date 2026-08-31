#include "imgui/imgui_host.h"

#include "artichoco/core/application.h"
#include "artichoco/core/window.h"
#include "artichoco/platform/window/sdl_window.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <cstddef>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace arti::engine {
namespace {

const core::Logger::Channel& log() { return core::Application::get().getLogChannel(); }

platform::SDLWindow& requireSDLWindow(core::Window& window) {
    auto* sdl_window = dynamic_cast<platform::SDLWindow*>(&window);
    if (sdl_window == nullptr) {
        throw std::invalid_argument("ImGuiHost requires an SDL window.");
    }
    return *sdl_window;
}

void setDarkThemeColors() {
    auto& style = ImGui::GetStyle();
    auto& colors = style.Colors;

    colors[ImGuiCol_WindowBg] = ImVec4{0.11f, 0.11f, 0.11f, 1.00f};
    colors[ImGuiCol_ChildBg] = ImVec4{0.11f, 0.11f, 0.11f, 1.00f};
    colors[ImGuiCol_PopupBg] = ImVec4{0.08f, 0.08f, 0.08f, 0.96f};
    colors[ImGuiCol_Border] = ImVec4{0.17f, 0.17f, 0.18f, 1.00f};

    colors[ImGuiCol_TitleBg] = ImVec4{0.07f, 0.07f, 0.07f, 1.00f};
    colors[ImGuiCol_TitleBgActive] = ImVec4{0.09f, 0.09f, 0.09f, 1.00f};
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4{0.07f, 0.07f, 0.07f, 1.00f};

    colors[ImGuiCol_FrameBg] = ImVec4{0.16f, 0.16f, 0.17f, 1.00f};
    colors[ImGuiCol_FrameBgHovered] = ImVec4{0.22f, 0.22f, 0.23f, 1.00f};
    colors[ImGuiCol_FrameBgActive] = ImVec4{0.13f, 0.13f, 0.14f, 1.00f};

    const ImVec4 orangeMain = ImVec4{0.92f, 0.45f, 0.11f, 1.00f};
    const ImVec4 orangeHovered = ImVec4{1.00f, 0.55f, 0.20f, 1.00f};
    const ImVec4 orangeActive = ImVec4{0.80f, 0.38f, 0.08f, 1.00f};

    colors[ImGuiCol_Button] = ImVec4{0.20f, 0.20f, 0.21f, 1.00f};
    colors[ImGuiCol_ButtonHovered] = orangeMain;
    colors[ImGuiCol_ButtonActive] = orangeActive;

    colors[ImGuiCol_Tab] = ImVec4{0.12f, 0.12f, 0.13f, 1.00f};
    colors[ImGuiCol_TabHovered] = orangeHovered;
    colors[ImGuiCol_TabActive] = orangeMain;
    colors[ImGuiCol_TabUnfocused] = ImVec4{0.12f, 0.12f, 0.13f, 1.00f};
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.18f, 0.18f, 0.19f, 1.00f};

    colors[ImGuiCol_CheckMark] = orangeMain;
    colors[ImGuiCol_SliderGrab] = orangeMain;
    colors[ImGuiCol_SliderGrabActive] = orangeActive;
    colors[ImGuiCol_Header] = ImVec4{0.35f, 0.20f, 0.08f, 0.50f};
    colors[ImGuiCol_HeaderHovered] = ImVec4{0.92f, 0.45f, 0.11f, 0.30f};
    colors[ImGuiCol_HeaderActive] = ImVec4{0.92f, 0.45f, 0.11f, 0.50f};

    colors[ImGuiCol_TextSelectedBg] = ImVec4{0.92f, 0.45f, 0.11f, 0.35f};

    colors[ImGuiCol_SeparatorHovered] = orangeMain;
    colors[ImGuiCol_SeparatorActive] = orangeActive;
    colors[ImGuiCol_ResizeGrip] = ImVec4{0.92f, 0.45f, 0.11f, 0.20f};
    colors[ImGuiCol_ResizeGripHovered] = orangeMain;
    colors[ImGuiCol_ResizeGripActive] = orangeActive;

    style.WindowRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(8, 8);
    style.ItemSpacing = ImVec2(8, 4);
}

}

ImGuiHost::ImGuiHost(core::Window& window, rendering::Renderer& renderer,
        const ImGuiHostCreateInfo& create_info)
        : m_window(requireSDLWindow(window)),
          m_renderer(renderer),
          m_docking(create_info.docking) {
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_context);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (m_docking) {
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }
    if (!create_info.persist_layout) {
        io.IniFilename = nullptr;
    }
    io.BackendRendererName = "artirenderer_imgui_pass";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::StyleColorsDark();
    setDarkThemeColors();

    if (!ImGui_ImplSDL3_InitForVulkan(m_window.nativeHandle())) {
        ImGui::DestroyContext(m_context);
        m_context = nullptr;
        throw std::runtime_error("Failed to initialize the Dear ImGui SDL3 backend.");
    }

    m_event_observer_id = m_window.addSDLEventObserver([this](const SDL_Event& event) {
        ImGui::SetCurrentContext(m_context);
        ImGui_ImplSDL3_ProcessEvent(&event);
    });
    if (m_event_observer_id == 0) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(m_context);
        m_context = nullptr;
        throw std::runtime_error("Failed to register the Dear ImGui SDL event observer.");
    }

    try {
        loadFont(create_info);
        createFontTexture();
    } catch (...) {
        m_window.removeSDLEventObserver(m_event_observer_id);
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(m_context);
        m_context = nullptr;
        throw;
    }
}

void ImGuiHost::loadFont(const ImGuiHostCreateInfo& create_info) {
    if (create_info.font_path.empty()) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    std::error_code error;
    if (!std::filesystem::exists(create_info.font_path, error)) {
        log().warn("UI font '{}' not found, falling back to the built-in font",
                create_info.font_path.string());
        return;
    }

    ImFontConfig config;
    // 图集是**静态**的：这个后端没有声明 ImGuiBackendFlags_RendererHasTextures，所以 ImGui
    // 走 1.92 之前那条路 —— 启动时把 glyph_ranges 指定的字形一次性烘完，运行期不再增长。
    // 换句话说范围之外的字会显示成方框，而不是按需补烘。真要按需补烘得让 ImGuiPass 处理
    // ImDrawData::Textures 的增删改，那是另一件事。
    const ImWchar* ranges = nullptr;
    if (create_info.chinese_glyphs) {
        // ASCII + 拉丁扩展 + 半角 + 日文假名 + 2500 个简体常用汉字。
        // 不用 GetGlyphRangesChineseFull()：那是 21000 个字形，图集会涨到几千像素见方、
        // 启动要烘好几秒，而编辑器里能出现的中文基本都在常用字里。
        //
        // 返回的是 ImGui 内部的静态数组，所以生命周期没问题 —— 这个指针要一直活到图集烘完，
        // 传一个局部数组进来是 ImFontConfig::GlyphRanges 最经典的坑。
        ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
        // 两千多个字形再横向 2 倍过采样，图集面积直接翻倍。CJK 字形本身笔画密，
        // 过采样带来的清晰度提升远不如拉丁文明显，所以这里按 1 来。
        config.OversampleH = 1;
        config.OversampleV = 1;
    }

    if (io.Fonts->AddFontFromFileTTF(create_info.font_path.string().c_str(),
                create_info.font_size_pixels, &config, ranges) == nullptr) {
        log().warn("Failed to load the UI font '{}', falling back to the built-in font",
                create_info.font_path.string());
        return;
    }
    log().info("Loaded the UI font '{}' at {}px ({})", create_info.font_path.filename().string(),
            create_info.font_size_pixels,
            create_info.chinese_glyphs ? "with common simplified Chinese" : "Latin only");
}

void ImGuiHost::createFontTexture() {
    ImGuiIO& io = ImGui::GetIO();

    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    int bytes_per_pixel = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        throw std::runtime_error("Dear ImGui produced an empty font atlas.");
    }

    const auto* bytes = reinterpret_cast<const std::byte*>(pixels);
    const auto byte_count = static_cast<size_t>(width) * static_cast<size_t>(height) *
                            static_cast<size_t>(bytes_per_pixel);

    rendering::TextureDesc desc;
    desc.texels = std::span{ bytes, byte_count };
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    desc.format = rendering::TextureFormat::RGBA8Unorm;
    desc.generate_mipmaps = false;
    desc.debug_name = "ImGui font atlas";
    m_font_texture = m_renderer.createTexture(desc);
    log().debug("ImGui font atlas is {}x{}", width, height);

    io.Fonts->SetTexID(rendering::imguiTextureId(m_font_texture));
}

ImGuiHost::~ImGuiHost() {
    m_window.removeSDLEventObserver(m_event_observer_id);
    if (m_context == nullptr) {
        return;
    }

    ImGui::SetCurrentContext(m_context);
    if (m_frame_started) {
        ImGui::EndFrame();
        m_frame_started = false;
    }
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(m_context);
    m_context = nullptr;

    if (m_font_texture.isValid()) {
        m_renderer.destroyTexture(m_font_texture);
        m_font_texture = {};
    }
}

void ImGuiHost::beginFrame() {
    if (m_frame_started) {
        throw std::logic_error("An ImGui frame is already active.");
    }

    ImGui::SetCurrentContext(m_context);
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    m_draw_data = nullptr;
    m_frame_started = true;
}

void ImGuiHost::endFrame() {
    if (!m_frame_started) {
        throw std::logic_error("No ImGui frame is active.");
    }

    ImGui::SetCurrentContext(m_context);
    ImGui::Render();
    m_draw_data = ImGui::GetDrawData();
    m_frame_started = false;
}

void ImGuiHost::dockSpaceOverViewport() {
    if (!m_docking) {
        return;
    }

    ImGui::SetCurrentContext(m_context);
    ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
}

rendering::FrameOverlay ImGuiHost::overlay() const noexcept {
    rendering::FrameOverlay overlay;
    overlay.imgui_draw_data = m_draw_data;
    return overlay;
}

bool ImGuiHost::wantsMouseInput() const noexcept {
    ImGui::SetCurrentContext(m_context);
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiHost::wantsKeyboardInput() const noexcept {
    ImGui::SetCurrentContext(m_context);
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiHost::wantsTextInput() const noexcept {
    ImGui::SetCurrentContext(m_context);
    return ImGui::GetIO().WantTextInput;
}

}
