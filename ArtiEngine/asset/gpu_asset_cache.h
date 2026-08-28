#pragma once
#include "arti_renderer.h"
#include "artichoco/asset/asset.h"

#include <unordered_map>

namespace arti::asset {
class AssetManager;
} // namespace arti::asset

namespace arti::engine::asset {

class MaterialAsset;
class MeshAsset;

// 资产（CPU 数据）和 renderer 句柄（GPU 资源）之间的那一层。
//
// 资产层刻意不认识渲染后端：MeshAsset 只有顶点和索引，没有任何 nvrhi 或 MeshHandle。
// 所以「按资产 UUID 拿到能画的东西」这件事需要有人做，就是这里。
//
// 缓存按资产 UUID 而不是 shared_ptr 地址：同一个资产被卸载又重新加载后地址会变，
// 但 UUID 不变，我们要的是「同一个资产只上传一次」。
class GpuAssetCache {
public:
    GpuAssetCache(arti::asset::AssetManager& assets, rendering::Renderer& renderer) noexcept;

    // 按资产 UUID 取 GPU 句柄，没上传过就现在上传。
    //
    // 返回无效句柄表示资产加载失败或类型不对 —— 调用方（extract）应该跳过那个 draw。
    // 这里不抛：一个坏资产不该让整帧画不出来。
    rendering::MeshHandle meshHandle(core::UUID asset);
    rendering::MaterialHandle materialHandle(core::UUID asset);

    // 丢掉所有缓存并销毁对应的 GPU 资源。换项目或者重新加载资产时调。
    void clear();

private:
    arti::asset::AssetManager* m_assets{ nullptr };
    rendering::Renderer* m_renderer{ nullptr };

    std::unordered_map<core::UUID, rendering::MeshHandle> m_meshes;
    std::unordered_map<core::UUID, rendering::MaterialHandle> m_materials;
    // 加载失败的资产记在这里，避免每帧重试一次并刷一遍日志。
    std::unordered_map<core::UUID, bool> m_failed;
};

} // namespace arti::engine::asset
