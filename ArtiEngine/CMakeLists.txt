add_library(artiengine STATIC
    scene/render_scene_extractor.cpp
)
add_library(ArtiEngine::Engine ALIAS artiengine)

target_compile_features(artiengine PUBLIC cxx_std_20)
target_include_directories(artiengine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(artiengine
    PUBLIC
        # 公开头里出现了 rendering:: 的类型和 scene::Scene，所以这两个是 PUBLIC。
        ArtiRenderer::Renderer
        ArtiChoco::Core
        ArtiChoco::Scene
        ArtiSDK::GLM
)

set_target_properties(artiengine PROPERTIES FOLDER "ArtiEngine")

# ImGui 宿主单独一个 target：它要链 SDL 和 imgui 的 SDL3 后端，不该让核心库的下游都背上。
# ArtiRenderer 不链 Platform/SDL，所以这部分只能待在引擎层。
add_library(artiengine_imgui STATIC
    imgui/imgui_host.cpp
)
add_library(ArtiEngine::ImGui ALIAS artiengine_imgui)

target_compile_features(artiengine_imgui PUBLIC cxx_std_20)
target_include_directories(artiengine_imgui
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(artiengine_imgui
    PUBLIC
        ArtiEngine::Engine
        # 公开头里没有 imgui 的类型（只有前向声明的 ImDrawData / ImGuiContext），
        # 但宿主画 UI 时需要 ImGui:: 那套 API，所以 Core 是 PUBLIC。
        ImGui::Core
    PRIVATE
        ArtiChoco::Platform
        ArtiSDK::SDL3
        ImGui::SDL3Backend
)

set_target_properties(artiengine_imgui PROPERTIES FOLDER "ArtiEngine")
