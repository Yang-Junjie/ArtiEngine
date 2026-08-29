#include "asset/importers/texture_importer.h"

#include "asset/detail/texture_artifact.h"

#include <stdexcept>
#include <utility>

namespace arti::engine::asset {

std::vector<std::string> TextureImporter::getSupportedExtensions() const {
    return { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".hdr" };
}

std::string TextureImporter::formatFromExistingMetadata(
        const std::optional<arti::asset::AssetMetadata>& existing) const {
    if (existing) {
        if (const auto found = existing->properties.find("format");
                found != existing->properties.end()) {
            if (const auto* value = std::get_if<std::string>(&found->second);
                    value != nullptr && detail::textureFormatFromName(*value)) {
                return *value;
            }
        }
    }
    return std::string{ detail::textureFormatName(rendering::TextureFormat::RGBA8Srgb) };
}

std::vector<std::byte> TextureImporter::encode(const arti::asset::AssetMetadata&,
        const std::filesystem::path& source_path) const {
    const auto file = resolveSourceFile(source_path);
    if (file.extension() == ".hdr" || file.extension() == ".HDR") {
        const auto image = detail::decodeImageRGBA16F(file);
        return detail::encodeTextureArtifact(image.rgba, image.width, image.height,
                rendering::TextureFormat::RGBA16Float, true);
    }
    const auto image = detail::decodeImageFile(file);
    return detail::encodeTextureArtifact(image.rgba, image.width, image.height,
            rendering::TextureFormat::RGBA8Srgb, true);
}

arti::asset::AssetImportResult TextureImporter::import(const std::filesystem::path& source_path) {
    arti::asset::AssetImportResult result;
    try {
        const auto file = resolveSourceFile(source_path);
        const bool is_hdr = file.extension() == ".hdr" || file.extension() == ".HDR";

        auto output = startOutput(source_path, "", kTextureAssetType, ".artitexture");

        const std::string format_name = is_hdr
                ? std::string{ detail::textureFormatName(rendering::TextureFormat::RGBA16Float) }
                : formatFromExistingMetadata(
                        m_catalog->findBySourcePathAndType(source_path,
                                std::string{ kTextureAssetType }));
        const auto format = detail::textureFormatFromName(format_name).value_or(
                rendering::TextureFormat::RGBA8Srgb);

        const auto image = is_hdr ? detail::decodeImageRGBA16F(file)
                                  : detail::decodeImageFile(file);

        output.metadata.properties["importer"] = std::string{ "artiengine.TextureImporter" };
        output.metadata.properties["width"] = static_cast<uint64_t>(image.width);
        output.metadata.properties["height"] = static_cast<uint64_t>(image.height);
        output.metadata.properties["data_size"] = static_cast<uint64_t>(image.rgba.size());
        output.metadata.properties["format"] = format_name;
        output.metadata.properties["flip_vertical"] = false;
        output.metadata.properties["generate_mipmaps"] = true;
        output.metadata.properties["filter"] = std::string{ "linear" };
        output.metadata.properties["address_u"] = std::string{ "repeat" };
        output.metadata.properties["address_v"] = std::string{ "repeat" };
        output.metadata.properties["max_anisotropy"] = 8.0;

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
