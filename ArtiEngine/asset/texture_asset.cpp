#include "asset/texture_asset.h"

#include <span>
#include <utility>

namespace arti::engine::asset {

TextureAsset::TextureAsset(core::UUID handle, std::vector<std::byte> texels, uint32_t width,
        uint32_t height, rendering::TextureFormat format, bool generate_mipmaps)
        : Asset(handle),
          m_texels(std::move(texels)),
          m_width(width),
          m_height(height),
          m_format(format),
          m_generate_mipmaps(generate_mipmaps) {}

arti::asset::AssetType TextureAsset::getType() const {
    return std::string{ kTextureAssetType };
}

rendering::TextureDesc TextureAsset::toTextureDesc(std::string debug_name) const {
    rendering::TextureDesc desc;
    desc.texels = std::span{ m_texels };
    desc.width = m_width;
    desc.height = m_height;
    desc.format = m_format;
    desc.generate_mipmaps = m_generate_mipmaps;
    desc.debug_name = std::move(debug_name);
    return desc;
}

}
