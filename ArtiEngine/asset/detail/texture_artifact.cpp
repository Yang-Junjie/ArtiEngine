#include "asset/detail/texture_artifact.h"

#include "asset/detail/artifact_io.h"

#include <bit>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <stb_image.h>

namespace arti::engine::asset::detail {
namespace {

constexpr size_t kHeaderSize = 24;

uint32_t formatToCode(rendering::TextureFormat format) {
    switch (format) {
        case rendering::TextureFormat::RGBA8Unorm:
            return 0;
        case rendering::TextureFormat::RGBA8Srgb:
            return 1;
        case rendering::TextureFormat::RGBA16Float:
            return 2;
    }
    return 1;
}

rendering::TextureFormat formatFromCode(uint32_t code) {
    switch (code) {
        case 0:
            return rendering::TextureFormat::RGBA8Unorm;
        case 2:
            return rendering::TextureFormat::RGBA16Float;
        default:
            return rendering::TextureFormat::RGBA8Srgb;
    }
}

size_t texelBytes(rendering::TextureFormat format) {
    switch (format) {
        case rendering::TextureFormat::RGBA8Unorm:
        case rendering::TextureFormat::RGBA8Srgb:
            return 4;
        case rendering::TextureFormat::RGBA16Float:
            return 8;
    }
    return 4;
}

uint16_t floatToHalf(float value) {
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    uint32_t mantissa = bits & 0x007fffffu;
    int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xffu) - 127 + 15;

    if (exponent <= 0) {
        if (exponent < -10) {
            return static_cast<uint16_t>(sign);
        }
        mantissa |= 0x00800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - exponent);
        const uint32_t rounded = (mantissa + (1u << (shift - 1))) >> shift;
        return static_cast<uint16_t>(sign | rounded);
    }
    if (exponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7c00u);
    }

    mantissa += 0x00001000u;
    if (mantissa & 0x00800000u) {
        mantissa = 0;
        ++exponent;
        if (exponent >= 31) {
            return static_cast<uint16_t>(sign | 0x7c00u);
        }
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
}

} // namespace

std::vector<std::byte> encodeTextureArtifact(const std::vector<std::byte>& pixels, uint32_t width,
        uint32_t height, rendering::TextureFormat format, bool generate_mipmaps) {
    const size_t expected = static_cast<size_t>(width) * height * texelBytes(format);
    if (width == 0 || height == 0 || pixels.size() != expected) {
        throw std::runtime_error(
                "The texture artifact needs width*height*texel bytes of pixel data.");
    }

    std::ostringstream stream{ std::ios::binary };
    stream.write(kTextureArtifactMagic.data(), kTextureArtifactMagic.size());
    writeU32(stream, kTextureArtifactVersion);
    writeU32(stream, width);
    writeU32(stream, height);
    writeU32(stream, formatToCode(format));
    writeU32(stream, generate_mipmaps ? 1u : 0u);
    stream.write(reinterpret_cast<const char*>(pixels.data()),
            static_cast<std::streamsize>(pixels.size()));

    const std::string text = stream.str();
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

std::shared_ptr<TextureAsset> decodeTextureArtifact(core::UUID handle,
        const std::vector<std::byte>& data) {
    if (data.size() < kHeaderSize) {
        throw std::runtime_error("The texture artifact is truncated.");
    }
    if (std::memcmp(data.data(), kTextureArtifactMagic.data(), kTextureArtifactMagic.size()) != 0) {
        throw std::runtime_error("The texture artifact has a wrong magic.");
    }
    const uint32_t version = readU32(data, 4);
    if (version != kTextureArtifactVersion) {
        throw std::runtime_error("The texture artifact version is " + std::to_string(version) +
                                 ", expected " + std::to_string(kTextureArtifactVersion) + ".");
    }

    const auto width = readU32(data, 8);
    const auto height = readU32(data, 12);
    const auto format = formatFromCode(readU32(data, 16));
    const bool generate_mipmaps = readU32(data, 20) != 0;

    const size_t pixel_bytes = static_cast<size_t>(width) * height * texelBytes(format);
    if (data.size() < kHeaderSize + pixel_bytes) {
        throw std::runtime_error("The texture artifact pixel data is truncated.");
    }

    std::vector<std::byte> pixels(pixel_bytes);
    std::memcpy(pixels.data(), data.data() + kHeaderSize, pixel_bytes);

    return std::make_shared<TextureAsset>(handle, std::move(pixels), width, height, format,
            generate_mipmaps);
}

DecodedImage decodeImageFile(const std::filesystem::path& file) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(file.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        throw std::runtime_error("Failed to decode the image '" + file.string() +
                                 "': " + (stbi_failure_reason() ? stbi_failure_reason() : "?"));
    }

    DecodedImage image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    const size_t byte_count = static_cast<size_t>(width) * height * 4;
    image.rgba.resize(byte_count);
    std::memcpy(image.rgba.data(), pixels, byte_count);
    stbi_image_free(pixels);
    return image;
}

DecodedImage decodeImageRGBA16F(const std::filesystem::path& file) {
    int width = 0;
    int height = 0;
    int channels = 0;
    float* pixels = stbi_loadf(file.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        throw std::runtime_error("Failed to decode the HDR image '" + file.string() +
                                 "': " + (stbi_failure_reason() ? stbi_failure_reason() : "?"));
    }

    DecodedImage image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    const size_t texel_count = static_cast<size_t>(width) * height;
    image.rgba.resize(texel_count * 8);
    for (size_t texel = 0; texel < texel_count; ++texel) {
        const size_t destination_offset = texel * 8;
        for (size_t channel = 0; channel < 4; ++channel) {
            const uint16_t half = floatToHalf(pixels[texel * 4 + channel]);
            std::memcpy(image.rgba.data() + destination_offset + channel * 2, &half, 2);
        }
    }
    stbi_image_free(pixels);
    return image;
}

std::string_view textureFormatName(rendering::TextureFormat format) {
    switch (format) {
        case rendering::TextureFormat::RGBA8Unorm:
            return "RGBA8Unorm";
        case rendering::TextureFormat::RGBA16Float:
            return "RGBA16Float";
        case rendering::TextureFormat::RGBA8Srgb:
            return "RGBA8Srgb";
    }
    return "RGBA8Srgb";
}

std::optional<rendering::TextureFormat> textureFormatFromName(std::string_view name) {
    if (name == "RGBA8Unorm") {
        return rendering::TextureFormat::RGBA8Unorm;
    }
    if (name == "RGBA8Srgb") {
        return rendering::TextureFormat::RGBA8Srgb;
    }
    if (name == "RGBA16Float") {
        return rendering::TextureFormat::RGBA16Float;
    }
    return std::nullopt;
}

} // namespace arti::engine::asset::detail
