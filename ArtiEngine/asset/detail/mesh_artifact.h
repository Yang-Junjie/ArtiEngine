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

// 顶点和 submesh 直接按内存布局读写，所以布局一变 artifact 就不兼容。
// 这几个断言让「改了 MeshVertex 忘了升 version」在编译期就停下来。
static_assert(sizeof(rendering::MeshVertex) == 56);
static_assert(sizeof(rendering::Submesh) == 20);

std::vector<std::byte> encodeMeshArtifact(const std::vector<rendering::MeshVertex>& vertices,
        const std::vector<uint32_t>& indices, const std::vector<rendering::Submesh>& submeshes,
        const std::vector<std::string>& material_slots);

// handle 由调用方给（来自 AssetMetadata），artifact 里不存 —— 身份属于 .meta，不属于数据。
std::shared_ptr<MeshAsset> decodeMeshArtifact(core::UUID handle,
        const std::vector<std::byte>& data);

} // namespace arti::engine::asset::detail
