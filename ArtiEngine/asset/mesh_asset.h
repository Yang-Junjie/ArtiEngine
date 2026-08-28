#pragma once
#include "arti_renderer.h"
#include "artichoco/asset/asset.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace arti::engine::asset {

inline constexpr std::string_view kMeshAssetType{ "artiengine.asset.mesh" };

// 纯几何：顶点、索引、submesh、以及每个 submesh 绑到哪个**材质槽名**。
//
// 网格不记「用哪个材质资产」—— 那由场景层的 MeshRendererComponent 按槽索引决定。
// 所以同一个网格能在不同实体上配不同材质，导入网格时也不要求材质已经存在。
class MeshAsset final : public arti::asset::Asset {
public:
    MeshAsset(core::UUID handle, std::vector<rendering::MeshVertex> vertices,
            std::vector<uint32_t> indices, std::vector<rendering::Submesh> submeshes,
            std::vector<std::string> material_slots, rendering::AABB bounds);

    arti::asset::AssetType getType() const override;

    const std::vector<rendering::MeshVertex>& vertices() const noexcept { return m_vertices; }
    const std::vector<uint32_t>& indices() const noexcept { return m_indices; }
    const std::vector<rendering::Submesh>& submeshes() const noexcept { return m_submeshes; }
    const std::vector<std::string>& materialSlots() const noexcept { return m_material_slots; }
    const rendering::AABB& bounds() const noexcept { return m_bounds; }

    rendering::Mesh toRenderMesh() const;

private:
    std::vector<rendering::MeshVertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<rendering::Submesh> m_submeshes;
    std::vector<std::string> m_material_slots;
    rendering::AABB m_bounds;
};

} // namespace arti::engine::asset
