#pragma once

#include "artichoco/core/uuid.h"

#include <sol/sol.hpp>

#include <string>
#include <unordered_set>

namespace arti::asset {
class AssetManager;
} // namespace arti::asset

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::engine::detail {

// 把 arti.* 和 entity userdata 绑进这个 state。scene / assets 以指针捕获，
// 调用方保证它们在这个 state 活着期间有效（一次模拟会话）。
void bindScriptApi(sol::state& lua, scene::Scene& scene, arti::asset::AssetManager* assets,
        std::unordered_set<std::string>& warned_keys);

sol::object makeEntityObject(sol::state& lua, scene::Scene& scene, core::UUID id);

} // namespace arti::engine::detail
