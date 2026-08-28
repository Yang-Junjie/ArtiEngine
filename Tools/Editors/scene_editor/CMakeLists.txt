add_executable(scene_editor
    src/application.cpp
    src/editor_layer.cpp
    src/editor_camera.cpp
    src/editor_gizmo.cpp
    src/editor_project.cpp
    src/file_dialogs.cpp
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
        # 项目的创建/加载和 Assets / Library\Artifacts 两个根目录都归 ProjectManager。
        ArtiChoco::Project
        ArtiSDK::GLM
        # 手柄只在编辑器里用，所以链在这里而不是 ArtiEngine::ImGui ——
        # 运行时应用不该因为「可能要画 UI」就背上 ImGuizmo。
        ImGuizmo::ImGuizmo
)

set_target_properties(scene_editor PROPERTIES
    FOLDER "Tools/Editors"
    OUTPUT_NAME "scene_editor"
)
