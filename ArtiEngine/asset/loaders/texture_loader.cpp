#include "asset/loaders/texture_loader.h"

#include "asset/detail/artifact_io.h"
#include "asset/detail/texture_artifact.h"
#include "asset/texture_asset.h"

#include <optional>

namespace arti::engine::asset {
namespace {

std::optional<rendering::TextureFormat> formatFromProperties(
        const std::unordered_map<std::string, arti::asset::Value>& properties) {
    const auto found = properties.find("format");
    if (found == properties.end()) {
        return std::nullopt;
    }
    if (const auto* value = std::get_if<std::string>(&found->second);
            value != nullptr) {
        return detail::textureFormatFromName(*value);
    }
    return std::nullopt;
}

std::optional<bool> mipmapsFromProperties(
        const std::unordered_map<std::string, arti::asset::Value>& properties) {
    const auto found = properties.find("generate_mipmaps");
    if (found == properties.end()) {
        return std::nullopt;
    }
    if (const auto* value = std::get_if<bool>(&found->second); value != nullptr) {
        return *value;
    }
    return std::nullopt;
}

}

arti::asset::AssetType TextureLoader::getType() const {
    return std::string{ kTextureAssetType };
}

std::shared_ptr<arti::asset::Asset> TextureLoader::decode(
        const arti::asset::AssetMetadata& metadata, const std::filesystem::path& artifact_file,
        std::span<const std::shared_ptr<arti::asset::Asset>> dependencies) {
    (void)dependencies;

    auto texture = detail::decodeTextureArtifact(metadata.handle,
            detail::readFileBinary(artifact_file));

    const auto format = formatFromProperties(metadata.properties).value_or(texture->format());
    const bool generate_mipmaps =
            mipmapsFromProperties(metadata.properties).value_or(texture->generateMipmaps());
    if (format != texture->format() || generate_mipmaps != texture->generateMipmaps()) {
        texture = std::make_shared<TextureAsset>(metadata.handle, texture->texels(),
                texture->width(), texture->height(), format, generate_mipmaps);
    }
    return texture;
}

}
