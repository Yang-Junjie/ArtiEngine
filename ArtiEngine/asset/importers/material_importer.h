#pragma once
#include "asset/importers/detail/engine_importer.h"
#include "asset/material_asset.h"

#include <string>

namespace arti::engine::asset {

// 编辑器创作的材质：`.artimaterial` 是真实源文件（YAML 文本），由这个 importer
// 编译成 artifact。这样它就是普通的 Root 资产 —— reconcile / 版本控制 / diff /
// 手改 / 外部工具生成全部自然成立，管线不需要"无源文件的用户资产"这种特例。
//
// 源文件里的贴图用**路径**引用（人可读、可 diff、可手改），importer 在导入时
// 解析成 UUID 写进 artifact。两种写法：
//   NormalTexture: Textures/rock_n.png              → 独立纹理资产
//   BaseColorTexture: Model/foo.gltf#texture.albedo → 容器的子资产
//
// 同时按槽位发布 Colorspace 推断，与 glTF / OBJ importer 一致 —— 所以在编辑器里
// 把一张图挂到 NormalTexture 上，那张图会自动按 linear 重导。
class MaterialImporter final : public detail::EngineImporter {
public:
    std::vector<std::string> getSupportedExtensions() const override;
    std::string getName() const override { return "artiengine.MaterialImporter"; }

    arti::asset::SourcePrescan prescan(
            const std::filesystem::path& source_path) const override;

    arti::asset::AssetImportResult import(
            const arti::asset::AssetImportRequest& request) override;
};

// 把材质参数写成 `.artimaterial` 源文本。Extract 和"新建材质"都用它。
// texture_paths 是五个槽位的引用字符串（空表示不绑）。
struct MaterialSourceTextures {
    std::string base_color;
    std::string metallic_roughness;
    std::string normal;
    std::string occlusion;
    std::string emissive;
};

std::string writeMaterialSource(const MaterialAsset::Params& params,
        const MaterialSourceTextures& textures);

}
