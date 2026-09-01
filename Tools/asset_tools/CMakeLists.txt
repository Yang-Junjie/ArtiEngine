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
endif()
