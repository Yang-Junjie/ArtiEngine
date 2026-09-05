add_library(arti_tools_asset STATIC
    asset_packer.cpp
    asset_pipeline.cpp
)
add_library(ArtiTools::Asset ALIAS arti_tools_asset)

target_compile_features(arti_tools_asset PUBLIC cxx_std_20)
target_include_directories(arti_tools_asset PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# Runtime 而不是 Engine：AssetPipeline 的公开头里有 engine::AssetRuntime 值成员。
# Project 也是 PUBLIC:打包要读 ProjectInfo，并用一个局部 ProjectManager 写产物里的 .artiproj。
target_link_libraries(arti_tools_asset PUBLIC ArtiEngine::Runtime ArtiChoco::Project)

set_target_properties(arti_tools_asset PROPERTIES FOLDER "Tools")

add_executable(asset_tools
    main.cpp
)
target_compile_features(asset_tools PRIVATE cxx_std_20)
target_link_libraries(asset_tools
    PRIVATE
        ArtiTools::Asset
        ArtiChoco::Core
        ArtiChoco::Project
)
set_target_properties(asset_tools PROPERTIES FOLDER "Tools")

if(BUILD_TESTING)
    add_executable(asset_pipeline_smoke tests/asset_pipeline_smoke.cpp)
    target_compile_features(asset_pipeline_smoke PRIVATE cxx_std_20)
    target_link_libraries(asset_pipeline_smoke PRIVATE ArtiTools::Asset ArtiChoco::Core)
    set_target_properties(asset_pipeline_smoke PROPERTIES FOLDER "Tests/Tools")
    add_test(NAME asset_pipeline_smoke COMMAND asset_pipeline_smoke)
    # ctest 不应该依赖 SDK 在 PATH 上：它链了 slang，缺 DLL 进程起不来。
    artichoco_stage_vulkan_sdk_runtime(asset_pipeline_smoke)
    artichoco_stage_msvc_runtime(asset_pipeline_smoke)

    # 脚本端到端。放在这里而不是 ArtiEngine 下：它需要 AssetPipeline（importer 只在那边注册）
    # 和 World 两个，而 ArtiEngine 的测试目标链不到 ArtiTools::Asset。
    add_executable(script_runtime_smoke tests/script_runtime_smoke.cpp)
    target_compile_features(script_runtime_smoke PRIVATE cxx_std_20)
    target_link_libraries(script_runtime_smoke PRIVATE ArtiTools::Asset ArtiChoco::Core)
    set_target_properties(script_runtime_smoke PROPERTIES FOLDER "Tests/Tools")
    add_test(NAME script_runtime_smoke COMMAND script_runtime_smoke)
    artichoco_stage_vulkan_sdk_runtime(script_runtime_smoke)
    artichoco_stage_msvc_runtime(script_runtime_smoke)

    # 脚本—物理桥接的端到端：固定步回调的节奏、arti.physics.* 的绑定、以及「同一个固定步里
    # 施力就生效」。和 script_runtime_smoke 同一个理由放在这里（要 AssetPipeline + World）。
    add_executable(script_physics_smoke tests/script_physics_smoke.cpp)
    target_compile_features(script_physics_smoke PRIVATE cxx_std_20)
    target_link_libraries(script_physics_smoke PRIVATE ArtiTools::Asset ArtiChoco::Core)
    set_target_properties(script_physics_smoke PROPERTIES FOLDER "Tests/Tools")
    add_test(NAME script_physics_smoke COMMAND script_physics_smoke)
    artichoco_stage_vulkan_sdk_runtime(script_physics_smoke)
    artichoco_stage_msvc_runtime(script_physics_smoke)
endif()
# 运行时依赖和内建 shader 拷到 exe 旁边。
#
# asset_tools 自己不渲染（从不建 RenderDevice），但：
#   - slang.dll 是静态导入的，缺了进程连加载都过不去
#   - pack 是从**自己 exe 旁边**取运行时文件的，所以 shaders/ 也得在
artichoco_stage_vulkan_sdk_runtime(asset_tools)
artichoco_stage_msvc_runtime(asset_tools)
artirenderer_stage_shaders(asset_tools)
