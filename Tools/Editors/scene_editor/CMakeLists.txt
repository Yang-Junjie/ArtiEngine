add_executable(scene_editor
    src/application.cpp
    src/editor_layer.cpp
    src/editor_camera.cpp
    src/hierarchy_panel.cpp
    src/inspector_panel.cpp
    src/viewport_panel.cpp
)

target_compile_features(scene_editor PRIVATE cxx_std_20)

target_link_libraries(scene_editor
    PRIVATE
        ArtiEngine::Engine
        ArtiEngine::ImGui
        ArtiChoco::Core
        ArtiChoco::Platform
        ArtiSDK::GLM
)

set_target_properties(scene_editor PROPERTIES
    FOLDER "Tools/Editors"
    OUTPUT_NAME "scene_editor"
)
