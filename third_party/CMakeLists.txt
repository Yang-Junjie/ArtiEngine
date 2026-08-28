# ImGuizmo：变换手柄。源码在 src/ 下（新版布局，不在仓库顶层）。
#
# 只编 ImGuizmo 本体。同目录下的 GraphEditor / ImSequencer / ImCurveEdit 等是独立的控件，
# 用不到就不编 —— 它们各自还有别的依赖。
add_library(imguizmo STATIC
    ImGuizmo/src/ImGuizmo.cpp
)
add_library(ImGuizmo::ImGuizmo ALIAS imguizmo)

target_compile_features(imguizmo PUBLIC cxx_std_20)
target_include_directories(imguizmo SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/ImGuizmo/src"
)

# ImGuizmo.cpp 里 include 了 imgui_internal.h —— 用的是 imgui 的私有 API，
# 所以不能只靠 ImGui::Core 的公开头路径。imgui 的根目录同时含公开和私有头，
# ImGui::Core 已经把它作为 SYSTEM PUBLIC 暴露出来了，链上就够。
target_link_libraries(imguizmo PUBLIC ImGui::Core)

set_target_properties(imguizmo PROPERTIES FOLDER "ThirdParty")
