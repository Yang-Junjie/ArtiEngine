add_library(artiengine STATIC
    asset/builtin_assets.cpp
    asset/detail/mesh_artifact.cpp
    asset/detail/prefab_artifact.cpp
    asset/detail/texture_artifact.cpp
    asset/gpu_asset_cache.cpp
    asset/importers/detail/gltf_parser.cpp
    asset/importers/gltf_importer.cpp
    asset/importers/obj_importer.cpp
    asset/importers/texture_importer.cpp
    asset/loaders/material_loader.cpp
    asset/loaders/mesh_loader.cpp
    asset/loaders/prefab_loader.cpp
    asset/loaders/texture_loader.cpp
    asset/material_asset.cpp
    asset/mesh_asset.cpp
    asset/prefab_asset.cpp
    asset/texture_asset.cpp
    scene/component_registration.cpp
    scene/component_serialization.cpp
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
        # OBJ / glTF 解析和图片解码只在 importer 的 .cpp 里用，不出现在公开头。
        TinyObjLoader::TinyObjLoader
        CGltf::CGltf
        stb::stb
)

set_target_properties(artiengine PROPERTIES FOLDER "ArtiEngine")

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
