#pragma once
#include "arti_renderer.h"
#include "artichoco/asset/asset.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace arti::engine {

namespace asset {
class MaterialAsset;
class MeshAsset;
class TextureAsset;
} // namespace asset

// 一个 MeshRendererComponent 画整个网格：每个 submesh 一个 draw，materials 按下标对应槽位
// （槽位顺序由 MeshAsset 的 material_slots 决定，导入时就定好了）。materials 比 submesh 少时
// 缺的那几段回退到默认材质，而不是不画。
struct MeshRendererComponent {
    arti::asset::AssetHandle<asset::MeshAsset> mesh;
    std::vector<arti::asset::AssetHandle<asset::MaterialAsset>> materials;

    bool visible{ true };
};

struct CameraComponent {
    float fov_degrees{ 60.0f };
    float near_plane{ 0.1f };
    float far_plane{ 100.0f };
    bool primary{ true };
};

struct DirectionalLightComponent {
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity{ 1.0f };
    bool enabled{ true };
};

// 场景的环境光照。不像灯光那样可以放好几个 —— 「环境」是唯一的那个背景，
// 抽取时只取遇到的第一个，多放的会被忽略。
//
// 字段和 rendering::EnvironmentDesc 一一对应，只是贴图那一项是资产引用而不是渲染句柄。
struct EnvironmentComponent {
    // 线性 HDR 的等距柱状投影源图。IBL 的输入 —— 现在还没有被渲染端消费，
    // 填了也只是被带到 RenderScene 上放着。
    arti::asset::AssetHandle<asset::TextureAsset> equirect_texture;
    // 没有环境贴图时的常数环境光。
    glm::vec3 sky_color{ 0.03f, 0.03f, 0.035f };
    // 线性倍率，不是光度学单位。
    float intensity{ 1.0f };
    bool enabled{ true };
};

} // namespace arti::engine
