# 工具链共用的平台相关代码。不放进 ArtiChoco::Platform ——
# 那里是运行时要的东西（窗口、surface），而这里是只有编辑器类工具才需要的原生对话框。
#
# 布局:common/ 放平台中立的头文件（接口),各平台目录放实现。分派在 CMake 这一层
# 按平台选源文件,所以实现里不带 #ifdef —— windows/file_dialogs.cpp 只有 Win32 代码。
add_library(arti_tools_platform STATIC)

if(WIN32)
    target_sources(arti_tools_platform PRIVATE windows/file_dialogs.cpp)
    # IFileDialog(ole32) + SHCreateItemFromParsingName(shell32)。CMake 的 Windows
    # 默认链接行里本来就有这两个,但一个库该自己声明依赖 —— 默认链接行不是契约。
    target_link_libraries(arti_tools_platform PRIVATE ole32 shell32)
else()
    # 目前只有 Windows 实现。这里直接失败而不是给个返回空路径的桩:
    # 桩会让编辑器正常起来但所有文件对话框静默失灵 —— 菜单点了没反应,
    # 比配置期就报错难查得多。加平台时在这里补一支 target_sources。
    message(FATAL_ERROR
        "arti_tools_platform: 目前只有 Windows 的 file_dialogs 实现,"
        "这个平台需要先补一份(见 common/file_dialogs.h 的接口)")
endif()

target_compile_features(arti_tools_platform PUBLIC cxx_std_20)

# PUBLIC:消费方写 #include "platform/common/file_dialogs.h"。
target_include_directories(arti_tools_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

set_target_properties(arti_tools_platform PROPERTIES FOLDER "Tools")

add_library(ArtiTools::Platform ALIAS arti_tools_platform)
