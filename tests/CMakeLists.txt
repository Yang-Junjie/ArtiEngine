# 编辑器需要真的 swapchain（Viewport 面板要拿到 SceneColor），所以这些用例开窗口。
# 纯 CI 容器里创建不了 Vulkan surface，打标签方便排除：ctest -LE requires-display
if(ARTIENGINE_BUILD_TOOLS)
    # 没有项目：菜单和面板要能画出来，用户才点得到 New / Open Project。
    #
    # 这个用例是补的 —— 之前所有用例都带 --auto-project，全都绕开了这条路，
    # 于是「没项目时 onRender 整帧 return，连 ImGui 都不提交」这个黑屏 bug 溜过了 3 个测试。
    # 卡 "0 draw calls" 就是判据：帧确实提交了（不是黑屏），但场景是空的（没项目）。
    add_test(NAME scene_editor_no_project_smoke
        COMMAND scene_editor --frames 30
    )
    set_tests_properties(scene_editor_no_project_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "First frame rendered \\(0 draw calls\\)")

    # 有项目的 Edit 模式：内置资产要能加载并上传，场景画出 3 个立方体。
    add_test(NAME scene_editor_edit_smoke
        COMMAND scene_editor --frames 30 --auto-project
    )
    set_tests_properties(scene_editor_edit_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "First frame rendered \\(3 draw calls\\)")

    # 跑到一半自动切进 Play 再切回来，覆盖 SceneCloner 的快照和恢复两条路。
    # 组件漏注册进拷贝表的话，日志里会出现 "component types skipped"。
    add_test(NAME scene_editor_play_smoke
        COMMAND scene_editor --frames 120 --auto-project --auto-play
    )

    # GPU 拾取：往 Viewport 正中反复发请求。没有这个用例的话 PickingPass 在自动化里
    # 永远不会启用（没人点鼠标），shader 都不编译，等于整条路没被覆盖。
    #
    # 只看「没报错」不够 —— 一直打空和「路径没跑」在没有命中记录时是分不开的。
    add_test(NAME scene_editor_pick_smoke
        COMMAND scene_editor --frames 60 --auto-project --auto-pick
    )
    set_tests_properties(scene_editor_pick_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "picking_id [0-9]+ \\(hit\\)")

    # 场景存读往返：存一次、清空、读回来、比对实体数和三种组件各自的数量，
    # 再确认 MeshRenderer 引用的资产 UUID 没变。
    #
    # 只比实体数不够 —— 组件漏注册进 SceneSerializationRegistry 时实体照样在，
    # 但那些组件不会被写进文件，读回来就没了。资产 UUID 也要比，那是「引用真的跨会话稳定」
    # 的唯一判据，也是先做资产层的理由。
    add_test(NAME scene_editor_scene_io_smoke
        COMMAND scene_editor --frames 40 --auto-project --auto-scene-io
    )
    set_tests_properties(scene_editor_scene_io_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "Scene IO check: round trip OK")

    set_tests_properties(scene_editor_no_project_smoke scene_editor_edit_smoke
        scene_editor_play_smoke scene_editor_pick_smoke scene_editor_scene_io_smoke
        PROPERTIES LABELS "requires-display")
endif()
