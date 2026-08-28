#include "asset/gpu_asset_cache.h"

#include "artichoco/asset/asset_manager.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"

namespace arti::engine::asset {

GpuAssetCache::GpuAssetCache(arti::asset::AssetManager& assets,
        rendering::Renderer& renderer) noexcept
        : m_assets(&assets),
          m_renderer(&renderer) {}

rendering::MeshHandle GpuAssetCache::meshHandle(core::UUID asset) {
    if (!asset.isValid() || m_failed.contains(asset)) {
        return {};
    }
    const auto cached = m_meshes.find(asset);
    if (cached != m_meshes.end()) {
        return cached->second;
    }

    const auto mesh_asset = m_assets->load<MeshAsset>(asset);
    if (!mesh_asset) {
        // AssetManager 已经记了具体原因的日志，这里只标记别再重试。
        m_failed.emplace(asset, true);
        return {};
    }

    const auto handle = m_renderer->createMesh(mesh_asset->toRenderMesh(), asset.toString());
    m_meshes.emplace(asset, handle);
    return handle;
}

rendering::MaterialHandle GpuAssetCache::materialHandle(core::UUID asset) {
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

    const auto handle = m_renderer->createMaterial(material_asset->toRenderMaterial());
    m_materials.emplace(asset, handle);
    return handle;
}

void GpuAssetCache::clear() {
    for (const auto& [asset, handle]: m_meshes) {
        m_renderer->destroyMesh(handle);
    }
    for (const auto& [asset, handle]: m_materials) {
        m_renderer->destroyMaterial(handle);
    }
    m_meshes.clear();
    m_materials.clear();
    // 失败记录也清掉：换项目之后同一个 UUID 可能是另一个资产，而且值得重试一次。
    m_failed.clear();
}

} // namespace arti::engine::asset
