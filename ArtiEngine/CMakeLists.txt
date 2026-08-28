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
