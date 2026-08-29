#pragma once
#include "arti_renderer.h"
#include "artichoco/asset/asset.h"

#include <unordered_map>

namespace arti::asset {
class AssetManager;
}

namespace arti::engine::asset {

class MaterialAsset;
class MeshAsset;
class TextureAsset;

class GPUAssetCache {
public:
    GPUAssetCache(arti::asset::AssetManager& assets, rendering::Renderer& renderer) noexcept;

    rendering::MeshHandle meshHandle(core::UUID asset);
    rendering::MaterialHandle materialHandle(core::UUID asset);
    rendering::TextureHandle textureHandle(core::UUID asset);
    void clear();

private:
    arti::asset::AssetManager* m_assets{ nullptr };
    rendering::Renderer* m_renderer{ nullptr };

    std::unordered_map<core::UUID, rendering::MeshHandle> m_meshes;
    std::unordered_map<core::UUID, rendering::MaterialHandle> m_materials;
    std::unordered_map<core::UUID, rendering::TextureHandle> m_textures;
    std::unordered_map<core::UUID, bool> m_failed;
};

}
