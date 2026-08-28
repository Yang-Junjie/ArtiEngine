#include "imgui/imgui_host.h"

#include "artichoco/core/window.h"
#include "artichoco/platform/window/sdl_window.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace arti::engine {
namespace {

platform::SDLWindow& requireSDLWindow(core::Window& window) {
    auto* sdl_window = dynamic_cast<platform::SDLWindow*>(&window);
    if (sdl_window == nullptr) {
        throw std::invalid_argument("ImGuiHost requires an SDL window.");
    }
    return *sdl_window;
}

} // namespace

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
        createFontTexture();
    } catch (...) {
        m_window.removeSDLEventObserver(m_event_observer_id);
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(m_context);
        m_context = nullptr;
        throw;
    }
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

} // namespace arti::engine
