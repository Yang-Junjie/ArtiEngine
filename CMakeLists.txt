cmake_minimum_required(VERSION 3.25)

project(ArtiEngine VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(PROJECT_IS_TOP_LEVEL)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    endif()
endif()

set(ARTIENGINE_ARTIRENDERER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ArtiRenderer")
# 嵌套 submodule：ArtiEngine -> ArtiRenderer -> ArtiChoco / imgui。少一层就在配置期直接报，
# 而不是等到编译时报一堆找不到头文件。
foreach(_arti_required_submodule
    "${ARTIENGINE_ARTIRENDERER_DIR}/CMakeLists.txt"
    "${ARTIENGINE_ARTIRENDERER_DIR}/ArtiChoco/CMakeLists.txt"
    "${ARTIENGINE_ARTIRENDERER_DIR}/third_party/imgui/imgui.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuizmo/src/ImGuizmo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/box3d/CMakeLists.txt"
)
    if(NOT EXISTS "${_arti_required_submodule}")
        message(FATAL_ERROR
            "Missing submodule file: ${_arti_required_submodule}\n"
            "Run: git submodule update --init --recursive")
    endif()
endforeach()

# SDK 模块在这里 include，让 ArtiSDK::* 和 artichoco_stage_vulkan_sdk_runtime() 对本项目每个
# 目录都可见。ArtiVulkanSDK.cmake 用了 include_guard(GLOBAL)，所以下游再 include 是空操作。
list(APPEND CMAKE_MODULE_PATH "${ARTIENGINE_ARTIRENDERER_DIR}/ArtiChoco/cmake")
include(ArtiVulkanSDK)
# CRT 的可再分发 DLL。产物用动态 CRT（/MD），而 msvcp140 / vcruntime140 那几个不是
# Windows 自带的，不带上就是「在装过 VS 的机器上能跑」。见模块开头的说明。
include(ArtiMsvcRuntime)

# ArtiRenderer 这里是被当依赖库消费的，它自己的 samples 和 tests 不需要跟着建。
set(ARTIRENDERER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ARTIRENDERER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(ArtiRenderer)

# third_party 在 ArtiRenderer 之后：ImGuizmo 要用 ArtiRenderer 里的 ImGui::Core（含私有头）。
add_subdirectory(third_party)

add_subdirectory(ArtiEngine)

# 独立播放器。放在 ArtiEngine 之后：它链 ArtiEngine::Runtime。
add_subdirectory(Runtime)

option(ARTIENGINE_BUILD_TOOLS "Build ArtiEngine tools (editors, etc.)" ON)
if(ARTIENGINE_BUILD_TOOLS)
    add_subdirectory(Tools)
endif()

