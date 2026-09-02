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
}

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

    // 投不投级联阴影。默认开：现有场景加载后立刻能看到效果，而不是要先去每个灯上打勾。
    // 目前只有第一个满足条件的方向光真的投影，其余照常照明。
    bool casts_shadow{ true };
    // 阴影覆盖到多远（世界单位）。越小越清晰，代价是这个距离之外没有阴影。
    float shadow_distance{ 100.0f };
};

// 点光源。位置来自 WorldTransformComponent，所以这里没有 position 字段。
struct PointLightComponent {
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    // 默认值刻意比方向光大得多：点光源带 1/d² 衰减，intensity 1 在 5 个单位远的地方只剩 0.04，
    // 加进来会像是没生效。25 让 5 个单位处大致等效于 intensity 1 的方向光。
    //
    // 这个数不是光度学单位（和 LightDesc::intensity 一样只是个纯倍数）—— 真要 lumen / candela
    // 那套，得连相机曝光一起改，见 TonemapPass。
    float intensity{ 25.0f };
    // 影响半径。衰减在这个距离上平滑地归零，不是硬截断 —— 所以它既是「照多远」也是将来做
    // 光源剔除的包围球半径。
    float range{ 10.0f };
    bool enabled{ true };
};

// 聚光灯。位置和朝向都来自 WorldTransformComponent（朝向是 -Z），和方向光一致。
struct SpotLightComponent {
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    // 和点光源同一个理由（1/d² 衰减），见那边的说明。
    float intensity{ 25.0f };
    float range{ 10.0f };
    // 内锥以内是满强度，内外之间平滑过渡，外锥以外全黑。
    //
    // 存角度而不是弧度：面板上直接编辑，序列化出来也读得懂。转弧度在 extractor 里做一次
    // —— rendering::LightDesc 那边是弧度。
    float inner_cone_degrees{ 20.0f };
    float outer_cone_degrees{ 30.0f };
    bool enabled{ true };
};

struct EnvironmentComponent {
    arti::asset::AssetHandle<asset::TextureAsset> equirect_texture;
    glm::vec3 sky_color{ 0.03f, 0.03f, 0.035f };
    float intensity{ 1.0f };
    bool enabled{ true };
    bool sky_visible{ true };
};

}
