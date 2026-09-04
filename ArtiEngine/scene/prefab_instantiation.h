#pragma once
#include "artichoco/scene/entity.h"

namespace arti::engine::asset {
class PrefabAsset;
} // namespace arti::engine::asset

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::engine {

// 按 PrefabAsset 的节点树在场景里生成实体，返回根（nodes[0] 对应的那个）。
//
// 这段逻辑以前只活在编辑器的 spawnAssetEntity 里。脚本要 spawn_prefab、播放器要在运行时
// 生成东西，都不能去 include 编辑器，所以下沉到 Engine —— 它只依赖 Scene + PrefabAsset，
// 不依赖 tick、物理、窗口。
//
// 空 prefab（一个节点都没有）返回无效 Entity，不往场景里塞东西。
// glm::decompose 失败的节点留下默认 transform 并记一条 warn，不让整棵树失败 ——
// 一棵树里偶发一个坏矩阵不该让其余节点也消失。
scene::Entity instantiatePrefab(scene::Scene& scene, const asset::PrefabAsset& prefab);

} // namespace arti::engine
