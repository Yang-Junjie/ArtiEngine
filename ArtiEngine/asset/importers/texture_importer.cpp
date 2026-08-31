#include "asset/importers/texture_importer.h"

#include "asset/detail/texture_artifact.h"

#include <stdexcept>
#include <utility>

namespace arti::engine::asset {

std::vector<std::string> TextureImporter::getSupportedExtensions() const {
    return { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".hdr" };
}

std::vector<arti::asset::SettingDescriptor> TextureImporter::getSettingSchema() const {
    arti::asset::SettingDescriptor colorspace;
    colorspace.key = kColorspaceSetting;
    // 默认 sRGB：单看一张图片无法判断用途，颜色数据是更常见的情况。
    // 容器（glTF）能按绑定的槽位推断出 linear，那条走 Inferred 层。
    colorspace.default_value = std::string{ kColorspaceSrgb };
    colorspace.allowed = { kColorspaceSrgb, kColorspaceLinear };
    colorspace.doc = "sRGB for color data (albedo, emissive); linear for data maps "
                     "(normal, metallic-roughness, occlusion).";
    return { std::move(colorspace) };
}

arti::asset::AssetImportResult TextureImporter::import(
        const arti::asset::AssetImportRequest& request) {
    const std::filesystem::path& source_path = request.source_path;
    arti::asset::AssetImportResult result;
    try {
        const auto file = resolveSourceFile(source_path);
        const bool is_hdr = file.extension() == ".hdr" || file.extension() == ".HDR";

        // local_id 为空：一张图片就是一个纹理资产，没有子资产。
        auto output = startOutput(source_path, {}, kTextureAssetType, ".artitexture");

        // HDR 一律 RGBA16F（本身就是线性浮点）；其余由 Colorspace 设置决定。
        // 设置来自 default → inferred → authored 的解析结果，所以这里不需要
        // fallback 分支。
        const bool linear = !is_hdr && request.settings != nullptr &&
                            request.settings->getString(kColorspaceSetting) == kColorspaceLinear;
        const auto format = is_hdr ? rendering::TextureFormat::RGBA16Float
                : linear          ? rendering::TextureFormat::RGBA8Unorm
                                  : rendering::TextureFormat::RGBA8Srgb;

        const auto image = is_hdr ? detail::decodeImageRGBA16F(file)
                                  : detail::decodeImageFile(file);

        // Properties 是导入结果的描述，不是输入。
        output.record.properties["width"] = static_cast<uint64_t>(image.width);
        output.record.properties["height"] = static_cast<uint64_t>(image.height);
        output.record.properties["data_size"] = static_cast<uint64_t>(image.rgba.size());
        output.record.properties["format"] = std::string{ detail::textureFormatName(format) };
        output.record.properties["generate_mipmaps"] = true;

        output.encoded = detail::encodeTextureArtifact(image.rgba, image.width, image.height,
                format, true);
        result.outputs.push_back(std::move(output));
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.outputs.clear();
    }
    return result;
}

}
