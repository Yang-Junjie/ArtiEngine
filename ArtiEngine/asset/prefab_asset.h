#pragma once
#include "artichoco/asset/asset.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>

namespace arti::engine::asset {

inline constexpr std::string_view kPrefabAssetType{ "artiengine.asset.prefab" };

inline constexpr uint32_t kNoParentNode = std::numeric_limits<uint32_t>::max();

struct PrefabNode {
    std::string name;
    glm::mat4 local_transform{ 1.0f };
    uint32_t parent{ kNoParentNode };

    core::UUID mesh;
    std::vector<core::UUID> materials;
};

class PrefabAsset final : public arti::asset::Asset {
public:
    PrefabAsset(core::UUID handle, std::vector<PrefabNode> nodes);

    arti::asset::AssetType getType() const override;

    const std::vector<PrefabNode>& nodes() const noexcept { return m_nodes; }

private:
    std::vector<PrefabNode> m_nodes;
};

}
