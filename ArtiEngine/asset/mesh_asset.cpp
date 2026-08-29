#include "asset/mesh_asset.h"

#include <utility>

namespace arti::engine::asset {

MeshAsset::MeshAsset(core::UUID handle, std::vector<rendering::MeshVertex> vertices,
        std::vector<uint32_t> indices, std::vector<rendering::Submesh> submeshes,
        std::vector<std::string> material_slots, rendering::AABB bounds)
        : Asset(handle),
          m_vertices(std::move(vertices)),
          m_indices(std::move(indices)),
          m_submeshes(std::move(submeshes)),
          m_material_slots(std::move(material_slots)),
          m_bounds(bounds) {}

arti::asset::AssetType MeshAsset::getType() const { return std::string{ kMeshAssetType }; }

rendering::Mesh MeshAsset::toRenderMesh() const {
    rendering::Mesh mesh;
    mesh.vertices = m_vertices;
    mesh.indices = m_indices;
    mesh.submeshes = m_submeshes;
    mesh.bounds = m_bounds;
    return mesh;
}

}
