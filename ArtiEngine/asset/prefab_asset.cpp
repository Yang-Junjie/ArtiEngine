#include "asset/prefab_asset.h"

#include <stdexcept>
#include <utility>

namespace arti::engine::asset {

PrefabAsset::PrefabAsset(core::UUID handle, std::vector<PrefabNode> nodes)
        : Asset(handle),
          m_nodes(std::move(nodes)) {
    for (size_t index = 0; index < m_nodes.size(); ++index) {
        const uint32_t parent = m_nodes[index].parent;
        if (parent == kNoParentNode) {
            continue;
        }
        if (parent >= index) {
            throw std::invalid_argument("Prefab node " + std::to_string(index) +
                                        " references parent " + std::to_string(parent) +
                                        ", which must come before it.");
        }
    }
}

arti::asset::AssetType PrefabAsset::getType() const {
    return std::string{ kPrefabAssetType };
}

}
