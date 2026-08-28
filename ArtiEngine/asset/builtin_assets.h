#pragma once
#include "artichoco/core/uuid.h"

namespace arti::asset {
class AssetManager;
} // namespace arti::asset

namespace arti::engine::asset {

// 内置资产：立方体网格和默认材质。
//
// UUID 写死，所以场景里存下的引用在任何项目、任何机器上都指向同一个东西。
// 值是任意选的常量，只要够随机不会和 UUID::generate() 撞就行。
inline constexpr core::UUID kBuiltinCubeMesh{ 0xB0117E1000000001ULL };
inline constexpr core::UUID kBuiltinDefaultMaterial{ 0xB0117E1000000002ULL };

// 把内置资产写成项目里真实的 .meta + artifact，然后注册进 catalog。
//
// 做成真资产而不是 AssetManager 之外的一张特殊表，是为了让下游完全无差别：
// Inspector 就是一个资产选择器，序列化就是存 UUID，extract 就是查同一个缓存。
// 否则 MeshRendererComponent 得同时表达「内置」和「资产」两种引用，那个分叉会渗透到
// 序列化、Inspector、extract 三处，而且 asset pipeline 成熟后还得拆掉。
//
// 幂等：已经存在且能加载就跳过。打开项目后调一次。
bool ensureBuiltinAssets(arti::asset::AssetManager& assets);

} // namespace arti::engine::asset
