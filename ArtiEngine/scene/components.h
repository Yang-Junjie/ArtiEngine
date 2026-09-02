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

// 刚体。**和 ColliderComponent 两个都有才会被模拟** —— 只有一个时建世界那步会 warn 并跳过，
// 不做「只有 collider 就当静态碰撞体」那种隐式创建（会让「为什么这东西会挡住我」变得不好查）。
//
// 物理还只作用于**没有父级、且缩放是 1** 的实体，见任务文档的 D3：物理在世界空间算，而
// TransformComponent 是局部的。这两种情况同样是 warn 并跳过。
struct RigidBodyComponent {
    // 和 Box3D 的 b3BodyType 一一对应，但刻意不直接用它的枚举：这个头是引擎的公开头，
    // 编辑器和 player 都包含它，不该让 box3d 的类型跟着扩散出去。
    enum class Type : uint8_t {
        Static = 0,     // 零质量、不动。地面和墙。
        Kinematic = 1,  // 零质量，但由用户驱动，会推开 dynamic 的东西。
        Dynamic = 2,    // 受重力和碰撞，唯一会自己动的一种。
    };

    Type type{ Type::Dynamic };
    // 这个刚体承受的重力倍数（无量纲）。0 是悬浮，负数往上飘。
    float gravity_scale{ 1.0f };
    // 静止一小会儿之后休眠：不再参与求解，也不再产生移动事件。关掉它每帧都会有事件，
    // 所以除了确实需要一直动的东西，别关。
    bool enable_sleep{ true };
};

// 碰撞体。材质属性（density / friction / restitution）挂在这里而不是刚体上，和 Box3D 一致 ——
// 将来做复合体（一个刚体多个碰撞体）时，各部分的材质本来就该各自不同。
struct ColliderComponent {
    enum class Shape : uint8_t {
        Box = 0,
        Sphere = 1,
        Capsule = 2,
    };

    Shape shape{ Shape::Box };

    // 三种形状的尺寸字段都常驻，只有 shape 选中的那些有意义（面板上也只显示那几行）。
    // 这样来回切形状不会把之前调好的值丢掉。
    //
    // 尺寸是显式写在这里的，**不从 TransformComponent 的 scale 推** —— 所以缩放过的实体会
    // 出现「看起来这么大、碰撞体那么大」，那也是 D3 里 warn 并跳过的原因。
    //
    // Box 用：三个方向的**半长**，和 b3MakeBoxHull 吃的东西一致（两边一致，省掉一次转换和
    // 一类 bug）。默认 0.5 正好是一个单位立方体。
    glm::vec3 half_extents{ 0.5f, 0.5f, 0.5f };
    // Sphere 和 Capsule 用。
    float radius{ 0.5f };
    // Capsule 用：中心到一端半球心的距离，**不含半径**，所以总高是 2 * (half_height + radius)。
    // Box3D 的 b3Capsule 存的是两个半球心，建 shape 时换算成 center1/2 = ±half_height * y。
    float half_height{ 0.5f };

    // kg/m³。dynamic 刚体至少要有一个非零密度的碰撞体，否则质量为零。
    float density{ 1.0f };
    // 库仑摩擦系数，常用 [0, 1]。0.3 抄的是 Box3D hello 里的值。
    float friction{ 0.3f };
    // 弹性系数。默认 0：不弹，掉下来就停住。
    float restitution{ 0.0f };
};

}
