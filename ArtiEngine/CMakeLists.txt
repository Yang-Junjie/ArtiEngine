add_library(artiengine STATIC
    asset/builtin_assets.cpp
    asset/detail/mesh_artifact.cpp
    asset/detail/prefab_artifact.cpp
    asset/detail/texture_artifact.cpp
    asset/gpu_asset_cache.cpp
    asset/importers/detail/gltf_parser.cpp
    asset/importers/gltf_importer.cpp
    asset/importers/material_importer.cpp
    asset/importers/script_importer.cpp
    asset/importers/texture_importer.cpp
    asset/loaders/material_loader.cpp
    asset/loaders/mesh_loader.cpp
    asset/loaders/prefab_loader.cpp
    asset/loaders/script_loader.cpp
    asset/loaders/texture_loader.cpp
    asset/material_asset.cpp
    asset/mesh_asset.cpp
    asset/prefab_asset.cpp
    asset/texture_asset.cpp
    engine_log.cpp
    scene/component_registration.cpp
    scene/component_serialization.cpp
    scene/prefab_instantiation.cpp
    scene/render_scene_extractor.cpp
)
add_library(ArtiEngine::Engine ALIAS artiengine)

target_compile_features(artiengine PUBLIC cxx_std_20)
target_include_directories(artiengine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(artiengine
    PUBLIC
        # 公开头里出现了 rendering:: 的类型和 scene::Scene，所以这两个是 PUBLIC。
        ArtiRenderer::Renderer
        ArtiChoco::Core
        ArtiChoco::Scene
        # 资产类型继承 arti::asset::Asset、loader 实现它的接口，所以也是 PUBLIC。
        ArtiChoco::Asset
        ArtiSDK::GLM
    PRIVATE
        # glTF 解析和图片解码只在 importer 的 .cpp 里用，不出现在公开头。
        CGltf::CGltf
        stb::stb
)

set_target_properties(artiengine PROPERTIES FOLDER "ArtiEngine")

# Runtime 层：场景 + 系统 tick + 一帧提交。编辑器的 Play 模式和独立 player 消费的是同一份，
# 否则「编辑器里 Play 的效果」和「exe 跑出来的效果」会各自漂移。
#
# 和 Engine 分开而不是并进去：Engine 是引擎的数据模型（资产、组件、抽取），Runtime 是驱动它
# 的循环。两者外部依赖完全一样，分开纯粹是为了让分层在链接关系上看得见 —— 以后 Runtime 要加
# 依赖（脚本 VM、物理）不必让所有消费 Engine 数据类型的目标跟着背。
add_library(artiengine_runtime STATIC
    runtime/asset_runtime.cpp
    runtime/detail/script_bindings.cpp
    runtime/physics_system.cpp
    runtime/scene_renderer.cpp
    runtime/script_system.cpp
    runtime/world.cpp
)
add_library(ArtiEngine::Runtime ALIAS artiengine_runtime)

target_compile_features(artiengine_runtime PUBLIC cxx_std_20)

target_link_libraries(artiengine_runtime
    PUBLIC
        # 公开头里有 RenderSceneExtractor（值成员）和 rendering:: 的类型，Engine 已经
        # PUBLIC 带上了 ArtiChoco::{Core,Scene,Asset} 和 ArtiRenderer::Renderer。
        ArtiEngine::Engine
    PRIVATE
        # PRIVATE：b3* 的类型只出现在 physics_system.cpp 里，公开头一个都没有（见那个头的注释）。
        # 下游因此只是跟着链上这个静态库，不会看见 box3d 的头。
        box3d
        # 同理：sol:: / lua_ 只出现在 script_system.cpp 和 detail/script_bindings.* 里。
        Sol2::Sol2
)

set_target_properties(artiengine_runtime PROPERTIES FOLDER "ArtiEngine")

if(BUILD_TESTING)
    # 只链 box3d，不链 artiengine_runtime：这个测试的意义就是在碰引擎之前先确认库本身是通的，
    # 后面物理出问题就不用怀疑到这一层。它也是 submodule 跟 main 的对冲 —— `git submodule
    # update --remote` 之后第一个报警的应该是它。
    add_executable(physics_smoke runtime/tests/physics_smoke.cpp)
    target_compile_features(physics_smoke PRIVATE cxx_std_20)
    target_link_libraries(physics_smoke PRIVATE box3d)
    set_target_properties(physics_smoke PROPERTIES FOLDER "Tests/ArtiEngine")
    add_test(NAME physics_smoke COMMAND physics_smoke)
    # 不用 stage Vulkan SDK：它一个 DLL 都不链。CRT 那套还是要的，和 asset_pipeline_smoke 一样。
    artichoco_stage_msvc_runtime(physics_smoke)

    # 场景快照的往返。和 physics_smoke 相反，这个**要**链 artiengine_runtime —— 它测的就是
    # World 自己。不用 stage Vulkan SDK：它链进了渲染类型，但从不创建 RenderDevice
    # （和 asset_tools 是同一种情况）。
    add_executable(scene_snapshot_smoke runtime/tests/scene_snapshot_smoke.cpp)
    target_compile_features(scene_snapshot_smoke PRIVATE cxx_std_20)
    target_link_libraries(scene_snapshot_smoke PRIVATE ArtiEngine::Runtime)
    set_target_properties(scene_snapshot_smoke PROPERTIES FOLDER "Tests/ArtiEngine")
    add_test(NAME scene_snapshot_smoke COMMAND scene_snapshot_smoke)
    artichoco_stage_msvc_runtime(scene_snapshot_smoke)

    # Prefab 实例化。链 Engine 不链 Runtime：它测的是 Scene + PrefabAsset，不该被物理 /
    # 脚本那一层拖着。
    add_executable(prefab_instantiate_smoke runtime/tests/prefab_instantiate_smoke.cpp)
    target_compile_features(prefab_instantiate_smoke PRIVATE cxx_std_20)
    target_link_libraries(prefab_instantiate_smoke PRIVATE ArtiEngine::Engine)
    set_target_properties(prefab_instantiate_smoke PROPERTIES FOLDER "Tests/ArtiEngine")
    add_test(NAME prefab_instantiate_smoke COMMAND prefab_instantiate_smoke)
    artichoco_stage_msvc_runtime(prefab_instantiate_smoke)

    # Lua VM 的冒烟。和 physics_smoke 同一个套路：**不链引擎**，只链 sol2（它 PUBLIC 带上 lua）。
    # 所以脚本层出问题时，这一层是不是通的不用再怀疑。它也是 lua submodule 换版本的对冲。
    add_executable(lua_vm_smoke runtime/tests/lua_vm_smoke.cpp)
    target_compile_features(lua_vm_smoke PRIVATE cxx_std_20)
    target_link_libraries(lua_vm_smoke PRIVATE Sol2::Sol2)
    set_target_properties(lua_vm_smoke PROPERTIES FOLDER "Tests/ArtiEngine")
    add_test(NAME lua_vm_smoke COMMAND lua_vm_smoke)
    artichoco_stage_msvc_runtime(lua_vm_smoke)

    add_executable(physics_raycast_smoke runtime/tests/physics_raycast_smoke.cpp)
    target_compile_features(physics_raycast_smoke PRIVATE cxx_std_20)
    target_link_libraries(physics_raycast_smoke PRIVATE ArtiEngine::Runtime)
    set_target_properties(physics_raycast_smoke PROPERTIES FOLDER "Tests/ArtiEngine")
    add_test(NAME physics_raycast_smoke COMMAND physics_raycast_smoke)
    artichoco_stage_msvc_runtime(physics_raycast_smoke)

    # transform 所有权：Dynamic 归物理，Static / Kinematic 归场景。这条错了不会崩，
    # 症状是「按住键物体先卡一下才动」—— 从现象反推很远，所以钉在这里。
    add_executable(physics_transform_ownership_smoke
        runtime/tests/physics_transform_ownership_smoke.cpp)
    target_compile_features(physics_transform_ownership_smoke PRIVATE cxx_std_20)
    target_link_libraries(physics_transform_ownership_smoke PRIVATE ArtiEngine::Runtime)
    set_target_properties(physics_transform_ownership_smoke PROPERTIES
        FOLDER "Tests/ArtiEngine")
    add_test(NAME physics_transform_ownership_smoke COMMAND physics_transform_ownership_smoke)
    artichoco_stage_msvc_runtime(physics_transform_ownership_smoke)
endif()

# ImGui 宿主单独一个 target：它要链 SDL 和 imgui 的 SDL3 后端，不该让核心库的下游都背上。
# ArtiRenderer 不链 Platform/SDL，所以这部分只能待在引擎层。
add_library(artiengine_imgui STATIC
    imgui/imgui_host.cpp
)
add_library(ArtiEngine::ImGui ALIAS artiengine_imgui)

target_compile_features(artiengine_imgui PUBLIC cxx_std_20)
target_include_directories(artiengine_imgui
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(artiengine_imgui
    PUBLIC
        ArtiEngine::Engine
        # 公开头里没有 imgui 的类型（只有前向声明的 ImDrawData / ImGuiContext），
        # 但宿主画 UI 时需要 ImGui:: 那套 API，所以 Core 是 PUBLIC。
        ImGui::Core
    PRIVATE
        ArtiChoco::Platform
        ArtiSDK::SDL3
        ImGui::SDL3Backend
)

set_target_properties(artiengine_imgui PROPERTIES FOLDER "ArtiEngine")
