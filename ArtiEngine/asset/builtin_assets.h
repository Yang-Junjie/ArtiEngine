#pragma once
#include "artichoco/core/uuid.h"

namespace arti::asset {
class AssetManager;
}

namespace arti::engine::asset {

inline constexpr core::UUID kBuiltinCubeMesh{ 0xB0117E1000000001ULL };
inline constexpr core::UUID kBuiltinDefaultMaterial{ 0xB0117E1000000002ULL };

// 把 builtin 资产登记进 catalog（AssetOrigin::Engine），并保证它们的 artifact
// 存在。同时注册成 AssetManager 的 engine asset provider，使每轮 reconcile
// 都能自愈被删掉的 builtin artifact。开工作区后调用一次。
bool ensureBuiltinAssets(arti::asset::AssetManager& assets);

// 只做登记与 artifact 补齐，不注册 provider。供 provider 回调自身使用。
bool restoreBuiltinAssets(arti::asset::AssetManager& assets);

}
