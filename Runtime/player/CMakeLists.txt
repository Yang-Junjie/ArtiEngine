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
