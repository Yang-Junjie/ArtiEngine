#include "runtime/physics_system.h"

#include "engine_log.h"
#include "scene/components.h"

#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <box3d/box3d.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace arti::engine {
namespace {

// 一个固定步长里的子步数。Box3D 推荐 1/60 + 4 子步（内部相当于 240 Hz 求解），而
// FixedTimestepAccumulator 的默认步长正好是 1/60 —— 两边不用互相迁就。
constexpr int kSubStepCount = 4;

// 非单位缩放的判定容差。
constexpr float kScaleTolerance = 1e-3f;

// 实体 id 直接塞进 userData 的指针位宽，**这是有意的** —— 省掉一张 id ↔ 索引的表。
static_assert(sizeof(void*) >= sizeof(core::UUID::Value),
        "userData 装不下实体 id，得改成存一个索引进自己的数组");

void* toUserData(core::UUID id) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(id.value()));
}

core::UUID fromUserData(void* user_data) {
    return core::UUID{ static_cast<core::UUID::Value>(
            reinterpret_cast<std::uintptr_t>(user_data)) };
}

b3Vec3 toBox3D(const glm::vec3& value) { return b3Vec3{ value.x, value.y, value.z }; }

glm::vec3 fromBox3D(const b3Vec3& value) { return glm::vec3{ value.x, value.y, value.z }; }

// b3Quat 是 .v（xyz 向量部分）+ .s（标量）两段，而 glm::quat 的构造是 (w, x, y, z)。
// **把 w 放错的表现是物体绕着奇怪的轴转，不是不动** —— 所以这个换算只写在这两个函数里，
// 别处一律调它们。
b3Quat toBox3D(const glm::quat& value) {
    return b3Quat{ b3Vec3{ value.x, value.y, value.z }, value.w };
}

glm::quat fromBox3D(const b3Quat& value) {
    return glm::quat{ value.s, value.v.x, value.v.y, value.v.z };
}

b3BodyType toBox3D(RigidBodyComponent::Type type) {
    switch (type) {
        case RigidBodyComponent::Type::Static:
            return b3_staticBody;
        case RigidBodyComponent::Type::Kinematic:
            return b3_kinematicBody;
        case RigidBodyComponent::Type::Dynamic:
            return b3_dynamicBody;
    }
    return b3_staticBody;
}

bool isUnitScale(const glm::vec3& scale) {
    return std::abs(scale.x - 1.0f) < kScaleTolerance &&
            std::abs(scale.y - 1.0f) < kScaleTolerance &&
            std::abs(scale.z - 1.0f) < kScaleTolerance;
}

void createShape(b3BodyId body, const ColliderComponent& collider) {
    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.density = collider.density;
    // 材质在 shapeDef 上**嵌了一层**：density 是平的，摩擦和弹性在 baseMaterial 里。
    shape_def.baseMaterial.friction = collider.friction;
    shape_def.baseMaterial.restitution = collider.restitution;

    switch (collider.shape) {
        case ColliderComponent::Shape::Box: {
            // b3MakeBoxHull 吃的就是半长，和组件里存的一致，不用换算。
            const b3BoxHull hull = b3MakeBoxHull(collider.half_extents.x,
                    collider.half_extents.y, collider.half_extents.z);
            b3CreateHullShape(body, &shape_def, &hull.base);
            break;
        }
        case ColliderComponent::Shape::Sphere: {
            const b3Sphere sphere{ b3Vec3{ 0.0f, 0.0f, 0.0f }, collider.radius };
            b3CreateSphereShape(body, &shape_def, &sphere);
            break;
        }
        case ColliderComponent::Shape::Capsule: {
            // b3Capsule 存的是**两个半球心**，不是「半高 + 半径」：立着的胶囊两心在 ±half_height。
            const b3Capsule capsule{ b3Vec3{ 0.0f, -collider.half_height, 0.0f },
                b3Vec3{ 0.0f, collider.half_height, 0.0f }, collider.radius };
            b3CreateCapsuleShape(body, &shape_def, &capsule);
            break;
        }
    }
}

} // namespace

struct PhysicsSystem::Impl {
    b3WorldId world{ b3_nullWorldId };
    // 上一次看到的帧号。见 onUpdate 里那段注释 —— 它同时是「新一次模拟开始了」的信号和
    // 「这一帧已经建过了」的保护。
    std::uint64_t last_frame_index{ 0 };
    // **场景拥有 transform** 的那些 body（Static / Kinematic），以及它们对应的实体。
    // 每个固定步之前从 TransformComponent 读一次推进物理世界 —— 见 syncSceneOwned()。
    std::vector<std::pair<b3BodyId, core::UUID>> scene_owned;

    ~Impl() { destroyWorld(); }

    bool hasWorld() const noexcept { return !B3_IS_NULL(world); }

    void destroyWorld() {
        if (hasWorld()) {
            b3DestroyWorld(world);
            world = b3_nullWorldId;
        }
    }

    void buildWorld(scene::Scene& scene);
    void syncSceneOwned(scene::Scene& scene);
    void step(scene::Scene& scene, float fixed_delta_time);
    std::optional<PhysicsSystem::RaycastHit> raycast(
            const glm::vec3& origin, const glm::vec3& translation) const;
};

void PhysicsSystem::Impl::buildWorld(scene::Scene& scene) {
    destroyWorld();
    scene_owned.clear();

    b3WorldDef world_def = b3DefaultWorldDef();
    world = b3CreateWorld(&world_def);

    int bodies = 0;
    int skipped = 0;
    for (auto [handle, id, transform, parent, body, collider]:
            scene.view<scene::IDComponent, scene::TransformComponent, scene::ParentComponent,
                            RigidBodyComponent, ColliderComponent>()
                    .each()) {
        // D3：物理在世界空间算，而 TransformComponent 是局部的 —— 带父级的实体要拿父级的世界
        // 逆矩阵反算才能写回，那是第二个问题。静默跳过会让人以为物理坏了，所以记一条 warn。
        if (parent.parent_id.isValid()) {
            getLogChannel().warn(
                    "Physics skips entity {}: a body with a parent is not supported yet.",
                    id.id.toString());
            ++skipped;
            continue;
        }
        // 形状尺寸写在 ColliderComponent 上、不从 scale 推，所以缩放过的实体会「看起来这么大、
        // 碰撞体那么大」。同样 warn 并跳过。
        if (!isUnitScale(transform.scale)) {
            getLogChannel().warn(
                    "Physics skips entity {}: collider sizes do not follow the Transform scale "
                    "({}, {}, {}).",
                    id.id.toString(), transform.scale.x, transform.scale.y, transform.scale.z);
            ++skipped;
            continue;
        }

        b3BodyDef body_def = b3DefaultBodyDef();
        body_def.type = toBox3D(body.type);
        // 没有父级，所以局部变换就是世界变换。
        body_def.position = toBox3D(transform.translation);
        body_def.rotation = toBox3D(transform.rotation);
        body_def.gravityScale = body.gravity_scale;
        body_def.enableSleep = body.enable_sleep;
        body_def.userData = toUserData(id.id);

        const b3BodyId body_id = b3CreateBody(world, &body_def);
        createShape(body_id, collider);
        // Static / Kinematic 的 transform 归场景（脚本、gizmo、动画都可能写它），所以记下来
        // 每步同步一次。Dynamic 不进这张表 —— 它的位置由求解器决定。
        if (body.type != RigidBodyComponent::Type::Dynamic) {
            scene_owned.emplace_back(body_id, id.id);
        }
        ++bodies;
    }

    // D4：两个组件都在才会被模拟。只有一个的时候不隐式补另一个（隐式创建会让「为什么这东西
    // 会挡住我」变得不好查），但也不能一声不响。
    for (auto [handle, id, body]:
            scene.view<scene::IDComponent, RigidBodyComponent>(entt::exclude<ColliderComponent>)
                    .each()) {
        getLogChannel().warn("Physics skips entity {}: it has a RigidBody but no Collider.",
                id.id.toString());
        ++skipped;
    }
    for (auto [handle, id, collider]:
            scene.view<scene::IDComponent, ColliderComponent>(entt::exclude<RigidBodyComponent>)
                    .each()) {
        getLogChannel().warn("Physics skips entity {}: it has a Collider but no RigidBody.",
                id.id.toString());
        ++skipped;
    }

    getLogChannel().debug("Physics world built: {} bodies ({} entities skipped)", bodies, skipped);
}

// 把 Static / Kinematic 的 transform 从场景推进物理世界。
//
// **这是 transform 所有权规则的一半**（另一半是 step() 里那个 Dynamic 判断）：
//   Dynamic          → 物理拥有 transform，写回场景
//   Static/Kinematic → 场景拥有 transform，物理只读它
//
// 没有这一步的话，脚本（或 gizmo、或将来的动画）写进 TransformComponent 的值会在下一个固定步
// 被物理的写回**盖掉** —— 而 body 自己根本不知道那次写入，因为它的位置只在 buildWorld() 时读过
// 一次。症状是「按住键，物体先卡一下才动」：脚本每帧加一点、物理每帧盖回去，直到那个 body 不再
// 出现在 moveEvents 里才停止打架。
void PhysicsSystem::Impl::syncSceneOwned(scene::Scene& scene) {
    for (const auto& [body_id, entity_id]: scene_owned) {
        auto entity = scene.findEntity(entity_id);
        if (!entity.isValid()) {
            // 模拟期间被删了。body 还在，下次重建时自然消失。
            continue;
        }
        const auto& transform = entity.getComponent<scene::TransformComponent>();
        b3Body_SetTransform(body_id, toBox3D(transform.translation),
                toBox3D(transform.rotation));
    }
}

void PhysicsSystem::Impl::step(scene::Scene& scene, float fixed_delta_time) {
    if (!hasWorld()) {
        return;
    }

    // 先读场景拥有的那些，再 step —— 顺序反了的话这一步用的还是上一帧的位置。
    syncSceneOwned(scene);

    b3World_Step(world, fixed_delta_time, kSubStepCount);

    // 只有这一步动过的 body 会出现在 moveEvents 里（文档明确说别每帧遍历所有 body）。这份数据
    // 到下一次 step 之前有效，所以当场消费完、不存指针。
    const b3BodyEvents events = b3World_GetBodyEvents(world);
    for (int index = 0; index < events.moveCount; ++index) {
        const b3BodyMoveEvent& move = events.moveEvents[index];
        // **只写回 Dynamic。**
        //
        // 诚实地说：**这一条不是承重的那一半** —— 有了上面的 syncSceneOwned()，非 Dynamic 的
        // body 在 step 之后本来就停在场景刚推给它的那个位置，写回等于把同一个值原样抄一遍。
        // 实测把这个判断删掉，`physics_transform_ownership_smoke` 照样全绿。
        //
        // 留着的两个理由：一是让所有权规则在代码里说得出来（读 step() 的人一眼看到「只有
        // Dynamic 写回」）；二是将来真给 Kinematic 接上速度驱动（b3Body_SetLinearVelocity）时，
        // 求解器会自己挪它，那时候写回就会和场景抢 —— 这道判断是那时候的防线。
        if (b3Body_GetType(move.bodyId) != b3_dynamicBody) {
            continue;
        }
        auto entity = scene.findEntity(fromUserData(move.userData));
        if (!entity.isValid()) {
            // 模拟期间实体被删了（Simulate 模式下随时可能）。body 还在物理世界里转，下一次
            // 重建时自然消失 —— 这里只是别去写一个不存在的实体。
            continue;
        }
        auto& transform = entity.getComponent<scene::TransformComponent>();
        transform.translation = fromBox3D(move.transform.p);
        transform.rotation = fromBox3D(move.transform.q);
        // scale 不动：物理不改缩放。
    }
}

PhysicsSystem::PhysicsSystem() : m_impl(std::make_unique<Impl>()) {}

PhysicsSystem::~PhysicsSystem() = default;

void PhysicsSystem::onUpdate(scene::Scene& scene, const scene::UpdateContext& context) {
    // 「新的一次模拟开始了」的信号：帧号不再单调递增。resetClock() 把它归零，而
    // enterPlayMode() 和 loadScene() 都会调 —— 两处都正好是该重建物理世界的时刻（D5）。
    //
    // 刻意不写成 frameIndex == 0：FixedUpdate 一帧里可能被调多次（追帧），那样第一帧会重建
    // 好几次。比帧号的单调性顺带就是「这一帧已经建过了」的那道保护。
    const bool restarted = !m_impl->hasWorld() || context.frameIndex < m_impl->last_frame_index;
    m_impl->last_frame_index = context.frameIndex;
    if (restarted) {
        m_impl->buildWorld(scene);
    }

    m_impl->step(scene, context.fixedDeltaTime);
}

std::optional<PhysicsSystem::RaycastHit> PhysicsSystem::Impl::raycast(
        const glm::vec3& origin, const glm::vec3& translation) const {
    if (!hasWorld()) {
        return std::nullopt;
    }

    const b3RayResult result = b3World_CastRayClosest(
            world, toBox3D(origin), toBox3D(translation), b3DefaultQueryFilter());
    if (!result.hit) {
        return std::nullopt;
    }

    const b3BodyId body = b3Shape_GetBody(result.shapeId);
    PhysicsSystem::RaycastHit hit;
    hit.entity = fromUserData(b3Body_GetUserData(body));
    hit.point = fromBox3D(result.point);
    hit.normal = fromBox3D(result.normal);
    hit.fraction = result.fraction;
    return hit;
}

std::optional<PhysicsSystem::RaycastHit> PhysicsSystem::raycast(
        const glm::vec3& origin, const glm::vec3& translation) const {
    return m_impl->raycast(origin, translation);
}

} // namespace arti::engine
