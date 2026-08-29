#include "asset/gpu_asset_cache.h"

#include "artichoco/asset/asset_manager.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "asset/texture_asset.h"

namespace arti::engine::asset {

GPUAssetCache::GPUAssetCache(arti::asset::AssetManager& assets,
        rendering::Renderer& renderer) noexcept
        : m_assets(&assets),
          m_renderer(&renderer) {}

rendering::MeshHandle GPUAssetCache::meshHandle(core::UUID asset) {
    if (!asset.isValid() || m_failed.contains(asset)) {
        return {};
    }
    const auto cached = m_meshes.find(asset);
    if (cached != m_meshes.end()) {
        return cached->second;
    }

    const auto mesh_asset = m_assets->load<MeshAsset>(asset);
    if (!mesh_asset) {
        m_failed.emplace(asset, true);
        return {};
    }

    const auto handle = m_renderer->createMesh(mesh_asset->toRenderMesh(), asset.toString());
    m_meshes.emplace(asset, handle);
    return handle;
}

rendering::MaterialHandle GPUAssetCache::materialHandle(core::UUID asset) {
    if (!asset.isValid() || m_failed.contains(asset)) {
        return {};
    }
    const auto cached = m_materials.find(asset);
    if (cached != m_materials.end()) {
        return cached->second;
    }

    const auto material_asset = m_assets->load<MaterialAsset>(asset);
    if (!material_asset) {
        m_failed.emplace(asset, true);
        return {};
    }

    rendering::Material material = material_asset->toRenderMaterial();
    const auto& params = material_asset->params();
    material.base_color_texture = textureHandle(params.base_color_texture.id());
    material.metallic_roughness_texture = textureHandle(params.metallic_roughness_texture.id());
    material.normal_texture = textureHandle(params.normal_texture.id());
    material.occlusion_texture = textureHandle(params.occlusion_texture.id());
    material.emissive_texture = textureHandle(params.emissive_texture.id());

    const auto handle = m_renderer->createMaterial(material);
    m_materials.emplace(asset, handle);
    return handle;
}

rendering::TextureHandle GPUAssetCache::textureHandle(core::UUID asset) {
    if (!asset.isValid() || m_failed.contains(asset)) {
        return {};
    }
    const auto cached = m_textures.find(asset);
    if (cached != m_textures.end()) {
        return cached->second;
    }

    const auto texture_asset = m_assets->load<TextureAsset>(asset);
    if (!texture_asset) {
        m_failed.emplace(asset, true);
        return {};
    }

    const auto handle = m_renderer->createTexture(texture_asset->toTextureDesc(asset.toString()));
    m_textures.emplace(asset, handle);
    return handle;
}

void GPUAssetCache::clear() {
    for (const auto& [asset, handle]: m_meshes) {
        m_renderer->destroyMesh(handle);
    }
    for (const auto& [asset, handle]: m_materials) {
        m_renderer->destroyMaterial(handle);
    }
    for (const auto& [asset, handle]: m_textures) {
        m_renderer->destroyTexture(handle);
    }
    m_meshes.clear();
    m_materials.clear();
    m_textures.clear();
    m_failed.clear();
}

}
