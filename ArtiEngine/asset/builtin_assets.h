#pragma once
#include "artichoco/core/uuid.h"

namespace arti::asset {
class AssetManager;
}

namespace arti::engine::asset {

inline constexpr core::UUID kBuiltinCubeMesh{ 0xB0117E1000000001ULL };
inline constexpr core::UUID kBuiltinDefaultMaterial{ 0xB0117E1000000002ULL };

bool ensureBuiltinAssets(arti::asset::AssetManager& assets);

}
