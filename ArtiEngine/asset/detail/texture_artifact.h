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
// RGBA8，按源文件格式（png/jpg/...）解码。
DecodedImage decodeImageFile(const std::filesystem::path& file);
// RGBA16F，HDR 图片专用（stbi_loadf + 半精度转换）。
DecodedImage decodeImageRGBA16F(const std::filesystem::path& file);
// 同上，但输入是内存里的编码字节。glTF 的内嵌图片（.glb 的 buffer view、data URI）
// 没有独立文件，只能走这条。
DecodedImage decodeImageMemory(std::span<const std::byte> bytes);
DecodedImage decodeImageMemoryRGBA16F(std::span<const std::byte> bytes);

// 格式名 ↔ 枚举，属性里存的是字符串名。
std::string_view textureFormatName(rendering::TextureFormat format);
std::optional<rendering::TextureFormat> textureFormatFromName(std::string_view name);

} // namespace arti::engine::asset::detail
