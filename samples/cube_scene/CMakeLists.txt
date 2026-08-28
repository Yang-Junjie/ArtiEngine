add_executable(cube_scene
    application.cpp
    cube_scene_layer.cpp
)

target_include_directories(cube_scene PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cube_scene
    PRIVATE
        ArtiEngine::Engine
        ArtiChoco::Platform
        ArtiSDK::GLM
)

artichoco_stage_vulkan_sdk_runtime(cube_scene)
set_target_properties(cube_scene PROPERTIES FOLDER "Samples")
