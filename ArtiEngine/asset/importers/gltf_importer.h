#pragma once
#include "asset/importers/detail/engine_importer.h"

namespace arti::engine::asset {

class GltfImporter final : public detail::EngineImporter {
public:
    // Extract 的记录位置：`ExtractedMaterial.<local_id>` = `.artimaterial` 源路径。
    // 派生材质是只读的，用户要改就把它提取成独立 Root 资产；这个设置让 prefab
    // 在重导入之后仍然指向提取物，而不是每次都回到容器自己产出的那份。
    static constexpr const char* kExtractedMaterialPrefix = "ExtractedMaterial.";

    std::vector<std::string> getSupportedExtensions() const override;
    std::string getName() const override { return "artiengine.GltfImporter"; }
    std::vector<arti::asset::SettingDescriptor> getSettingSchema() const override;

    // 声明引用的外部贴图与提取出的材质，并按材质槽位推断贴图的 Colorspace。
    arti::asset::SourcePrescan prescan(
            const std::filesystem::path& source_path) const override;

    arti::asset::AssetImportResult import(
            const arti::asset::AssetImportRequest& request) override;
};

}
