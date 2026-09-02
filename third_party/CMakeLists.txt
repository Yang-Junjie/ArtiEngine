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

# cgltf：单头文件的 glTF 2.0 解析器。选它而不是 tinygltf 是因为零额外依赖
# （tinygltf 要拉 nlohmann/json 进来），而且 accessor 读取有现成的 helper ——
# componentType × normalized 的那堆组合不用自己拆。
#
# 实现宏只能在**一个**翻译单元里定义，所以给个 .cpp 而不是纯接口库。
add_library(cgltf STATIC
    cgltf/cgltf.cpp
)
add_library(CGltf::CGltf ALIAS cgltf)

target_compile_features(cgltf PUBLIC cxx_std_20)
target_include_directories(cgltf SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/cgltf"
)
set_target_properties(cgltf PROPERTIES FOLDER "ThirdParty")

# stb_image：图片解码。源码在 ArtiChoco 的 third_party 下，但它自己不 add_subdirectory
# （ArtiChoco 用不到），所以在这里挂进来。
add_subdirectory("${ARTIENGINE_ARTIRENDERER_DIR}/ArtiChoco/third_party/stb"
    "${CMAKE_CURRENT_BINARY_DIR}/stb" EXCLUDE_FROM_ALL)
set_target_properties(stb PROPERTIES FOLDER "ThirdParty")

# Box3D：刚体物理。Erin Catto 的 3D 引擎，C17，除 C 运行时外无依赖。
#
# submodule 跟 main（`branch = main`）而不是 pin tag —— 它还是 alpha，修复来得快。升级是显式
# 动作：`git submodule update --remote third_party/box3d`，过一遍 physics_smoke，指针单独提交。
#
# samples / test / benchmark / docs 都被它自己的 PROJECT_IS_TOP_LEVEL 挡住了，所以作为
# submodule 消费时只会建 box3d 这一个 target，不用我们关任何开关。
add_subdirectory(box3d)

# **必须把 CRT 掰回来。** box3d 的 CMakeLists 顶上无条件写了
#     set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# 也就是静态 CRT（/MT），注释说是为了方便查内存泄漏。那个 set 在它自己的目录作用域里生效，
# 不会漏到我们这边，但它自己的 objects 会带上静态 CRT。
#
# 而本项目其余部分是动态 CRT（compile_commands 里是 `-D_DLL --dependent-lib=msvcrtd`），
# 而且打包产物专门带了 msvcp140 那几个 DLL。**/MT 和 /MD 混进一个二进制会在链接期或运行期炸**
# （轻则 LNK4098 defaultlib 冲突，重则跨 CRT 的堆不匹配）。
#
# 注意这条不能靠「clang 走 GNU 前端所以 MSVC 是空的、设置无效」蒙过去 —— 实测 CMake 在这条
# 工具链上照样把它翻译成 `--dependent-lib=`，所以是真的会生效。
set_target_properties(box3d PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    FOLDER "ThirdParty"
)
