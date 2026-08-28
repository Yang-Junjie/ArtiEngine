# 编辑器需要真的 swapchain（Viewport 面板要拿到 SceneColor），所以这些用例开窗口。
# 纯 CI 容器里创建不了 Vulkan surface，打标签方便排除：ctest -LE requires-display
if(ARTIENGINE_BUILD_TOOLS)
    # 只跑 Edit 模式：EditorCamera 提供视角，游戏逻辑的 stage 不跑。
    add_test(NAME scene_editor_edit_smoke
        COMMAND scene_editor --frames 30
    )

    # 跑到一半自动切进 Play 再切回来，覆盖 SceneCloner 的快照和恢复两条路。
    # 组件漏注册进拷贝表的话，日志里会出现 "component types skipped"。
    add_test(NAME scene_editor_play_smoke
        COMMAND scene_editor --frames 120 --auto-play
    )

    # GPU 拾取：往 Viewport 正中反复发请求。没有这个用例的话 PickingPass 在自动化里
    # 永远不会启用（没人点鼠标），shader 都不编译，等于整条路没被覆盖。
    #
    # 日志里应该出现 "picking_id 2 (hit)" —— 只看「没报错」不够，
    # 一直打空和「路径没跑」在没有命中记录时是分不开的。
    add_test(NAME scene_editor_pick_smoke
        COMMAND scene_editor --frames 60 --auto-pick
    )
    set_tests_properties(scene_editor_pick_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "picking_id [0-9]+ \\(hit\\)")

    set_tests_properties(scene_editor_edit_smoke scene_editor_play_smoke scene_editor_pick_smoke
        PROPERTIES LABELS "requires-display")
endif()
