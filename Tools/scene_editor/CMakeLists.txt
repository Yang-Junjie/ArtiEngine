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
    src/panels/project_settings_panel.cpp
    src/panels/viewport_panel.cpp
)

target_compile_features(scene_editor PRIVATE cxx_std_20)

# src 进搜索路径,所以面板写 "panels/xxx.h" 而不是 "../xxx.h" ——
# 后者的层数会随文件挪窝而失效。
target_include_directories(scene_editor PRIVATE src)

# UI 字体的**回落**路径，和 ARTIRENDERER_SHADER_DIR 一个套路：优先用 exe 旁边 POST_BUILD
# 拷过去的那份（见文件末尾），没有才回落到这里。保留源码树这条是为了换字体不用重编 C++；
# 两段查找在 src/editor_layer.cpp 里。
target_compile_definitions(scene_editor PRIVATE
    ARTIENGINE_TOOLS_RES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources"
)

target_link_libraries(scene_editor
    PRIVATE
        ArtiEngine::Engine
        # World / SceneRenderer —— 和独立 player 消费的是同一份。
        ArtiEngine::Runtime
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
# 运行时依赖和内建 shader 拷到 exe 旁边，和 arti_player 一致。
artichoco_stage_vulkan_sdk_runtime(scene_editor)
artirenderer_stage_shaders(scene_editor)

# UI 资源（字体）拷到 exe 旁边。ARTIENGINE_TOOLS_RES_DIR 只是回落，优先用这一份，
# 见 src/editor_layer.cpp 里的两段查找。
add_custom_command(TARGET scene_editor POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../resources"
        "$<TARGET_FILE_DIR:scene_editor>/resources"
    VERBATIM
)
