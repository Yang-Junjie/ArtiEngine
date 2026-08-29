#pragma once
#include "asset/texture_asset.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace arti::engine::asset::detail {

inline constexpr std::array<char, 4> kTextureArtifactMagic{ 'T', 'E', 'X', 'A' };
inline constexpr uint32_t kTextureArtifactVersion = 1;

std::vector<std::byte> encodeTextureArtifact(const std::vector<std::byte>& pixels, uint32_t width,
        uint32_t height, rendering::TextureFormat format, bool generate_mipmaps);

std::shared_ptr<TextureAsset> decodeTextureArtifact(core::UUID handle,
        const std::vector<std::byte>& data);

struct DecodedImage {
    std::vector<std::byte> rgba;
    uint32_t width{ 0 };
    uint32_t height{ 0 };
};
DecodedImage decodeImageFile(const std::filesystem::path& file);
DecodedImage decodeImageRGBA16F(const std::filesystem::path& file);
DecodedImage decodeImageMemory(std::span<const std::byte> bytes);
DecodedImage decodeImageMemoryRGBA16F(std::span<const std::byte> bytes);

std::string_view textureFormatName(rendering::TextureFormat format);
std::optional<rendering::TextureFormat> textureFormatFromName(std::string_view name);

}
