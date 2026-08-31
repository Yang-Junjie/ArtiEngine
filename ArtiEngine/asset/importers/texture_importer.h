#pragma once
#include "asset/importers/detail/engine_importer.h"

namespace arti::engine::asset {

class TextureImporter final : public detail::EngineImporter {
public:
    // 颜色空间。sRGB 用于 albedo / emissive 这类颜色数据，linear 用于
    // 法线、metalRoughness、AO 这类数据贴图 —— 走错会让光照结果不对，
    // 因为采样是硬件 sRGB 视图做的解码，着色器里没有任何手动转换。
    static constexpr const char* kColorspaceSetting = "Colorspace";
    static constexpr const char* kColorspaceSrgb = "srgb";
    static constexpr const char* kColorspaceLinear = "linear";

    std::vector<std::string> getSupportedExtensions() const override;
    std::string getName() const override { return "artiengine.TextureImporter"; }
    std::vector<arti::asset::SettingDescriptor> getSettingSchema() const override;

    arti::asset::AssetImportResult import(
            const arti::asset::AssetImportRequest& request) override;
};

}
