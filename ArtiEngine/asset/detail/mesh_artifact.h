#pragma once
#include "asset/detail/artifact_io.h"
#include "asset/mesh_asset.h"

#include <array>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace arti::engine::asset::detail {

inline constexpr std::array<char, 4> kMeshArtifactMagic{ 'M', 'S', 'H', 'A' };
inline constexpr uint32_t kMeshArtifactVersion = 1;

static_assert(sizeof(rendering::MeshVertex) == 56);
static_assert(sizeof(rendering::Submesh) == 20);

std::vector<std::byte> encodeMeshArtifact(const std::vector<rendering::MeshVertex>& vertices,
        const std::vector<uint32_t>& indices, const std::vector<rendering::Submesh>& submeshes,
        const std::vector<std::string>& material_slots);

std::shared_ptr<MeshAsset> decodeMeshArtifact(core::UUID handle,
        const std::vector<std::byte>& data);

}
