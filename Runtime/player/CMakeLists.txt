add_executable(arti_player
    src/application.cpp
    src/player_layer.cpp
)

target_compile_features(arti_player PRIVATE cxx_std_20)

target_include_directories(arti_player PRIVATE src)

target_link_libraries(arti_player
    PRIVATE
        # World / AssetRuntime / SceneRenderer —— 编辑器的 Play 模式消费的是同一份。
        ArtiEngine::Runtime
        # --stats 的调试覆盖层。默认不建 ImGui 上下文（见 PlayerLayer::onAttach），
        # 但链接是无条件的：一个链进来的 UI 后端不建上下文就不花运行时代价。
        ArtiEngine::ImGui
        ArtiChoco::Core
        # SDL 窗口和 Vulkan surface。
        ArtiChoco::Platform
        # .artiproj 的加载和 Assets / Library/Artifacts 两个根目录。
        ArtiChoco::Project
)

set_target_properties(arti_player PROPERTIES
    FOLDER "Runtime"
    OUTPUT_NAME "arti_player"
)
# 运行时依赖拷到 exe 旁边：SDL3 / slang 的 DLL（含 slang.dll 动态加载的
# slang-compiler.dll）和内建 shader。不拷的话产物只能在 SDK 在 PATH 上、且源码树
# 没挪窝的机器上跑 —— 而这个 exe 正是拿去发布的那一个。
artichoco_stage_vulkan_sdk_runtime(arti_player)
artirenderer_stage_shaders(arti_player)
