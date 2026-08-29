#pragma once
#include "arti_renderer.h"
#include "artichoco/asset/asset.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace arti::engine::asset {

inline constexpr std::string_view kTextureAssetType{ "artiengine.asset.texture" };

class TextureAsset final : public arti::asset::Asset {
public:
    TextureAsset(core::UUID handle, std::vector<std::byte> texels, uint32_t width, uint32_t height,
            rendering::TextureFormat format, bool generate_mipmaps);

    arti::asset::AssetType getType() const override;

    const std::vector<std::byte>& texels() const noexcept { return m_texels; }
    uint32_t width() const noexcept { return m_width; }
    uint32_t height() const noexcept { return m_height; }
    rendering::TextureFormat format() const noexcept { return m_format; }
    bool generateMipmaps() const noexcept { return m_generate_mipmaps; }

    rendering::TextureDesc toTextureDesc(std::string debug_name) const;

private:
    std::vector<std::byte> m_texels;
    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };
    rendering::TextureFormat m_format{ rendering::TextureFormat::RGBA8Srgb };
    bool m_generate_mipmaps{ true };
};

} // namespace arti::engine::asset
