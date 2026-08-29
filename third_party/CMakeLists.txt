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

# tiny_obj_loader：单头文件的 OBJ + MTL 解析器。
# 它同时暴露经典 MTL 字段（Kd/Ks/Ns）和 PBR 扩展（map_Pr/map_Pm/norm），
# 所以 importer 能按哪套有值来定 MaterialType，不用猜着映射。
#
# 实现要在**一个**翻译单元里定义 TINYOBJLOADER_IMPLEMENTATION，所以给个 .cpp 而不是纯接口库。
add_library(tinyobjloader STATIC
    tiny_obj_loader/tiny_obj_loader.cpp
)
add_library(TinyObjLoader::TinyObjLoader ALIAS tinyobjloader)

target_compile_features(tinyobjloader PUBLIC cxx_std_20)
target_include_directories(tinyobjloader SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/tiny_obj_loader"
)
set_target_properties(tinyobjloader PROPERTIES FOLDER "ThirdParty")

# stb_image：图片解码。源码在 ArtiChoco 的 third_party 下，但它自己不 add_subdirectory
# （ArtiChoco 用不到），所以在这里挂进来。
add_subdirectory("${ARTIENGINE_ARTIRENDERER_DIR}/ArtiChoco/third_party/stb"
    "${CMAKE_CURRENT_BINARY_DIR}/stb" EXCLUDE_FROM_ALL)
set_target_properties(stb PROPERTIES FOLDER "ThirdParty")
