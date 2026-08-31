add_executable(scene_editor
    src/application.cpp
    src/editor_layer.cpp
    src/editor_camera.cpp
    src/editor_context.cpp
    src/editor_gizmo.cpp
    src/editor_project.cpp
    src/scene_document.cpp
    src/panels/content_browser_panel.cpp
    src/panels/hierarchy_panel.cpp
    src/panels/inspector_panel.cpp
    src/panels/viewport_panel.cpp
)

target_compile_features(scene_editor PRIVATE cxx_std_20)

# src 进搜索路径,所以面板写 "panels/xxx.h" 而不是 "../xxx.h" ——
# 后者的层数会随文件挪窝而失效。
target_include_directories(scene_editor PRIVATE src)

# UI 字体直接从源码树读，和 ARTIRENDERER_SHADER_DIR 一个套路：换字体不用重新编译 C++，
# 代价是二进制不可搬移。编辑器是开发期工具，这个取舍是划算的；真要发布得改成拷到 exe 旁边。
target_compile_definitions(scene_editor PRIVATE
    ARTIENGINE_TOOLS_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../tools_asset"
)

target_link_libraries(scene_editor
    PRIVATE
        ArtiEngine::Engine
        ArtiEngine::ImGui
        ArtiTools::Asset
        # 原生文件对话框。工具专用,所以在 Tools/platform 而不是 ArtiChoco::Platform。
        ArtiTools::Platform
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
