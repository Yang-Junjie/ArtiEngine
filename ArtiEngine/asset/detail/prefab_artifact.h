#pragma once
#include "asset/prefab_asset.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace arti::engine::asset::detail {

std::vector<std::byte> encodePrefabArtifact(const std::vector<PrefabNode>& nodes);

std::shared_ptr<PrefabAsset> decodePrefabArtifact(core::UUID handle,
        const std::vector<std::byte>& data);

}
