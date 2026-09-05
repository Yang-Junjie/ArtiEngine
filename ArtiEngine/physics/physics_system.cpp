#include "physics/physics_system.h"

#include "engine_log.h"
#include "scene/components.h"

#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <box3d/box3d.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace arti::engine {
namespace {

// 一个固定步长里的子步数。Box3D 推荐 1/60 + 4 子步（内部相当于 240 Hz 求解），而
// FixedTimestepAccumulator 的默认步长正好是 1/60 —— 两边不用互相迁就。
constexpr int kSubStepCount = 4;

// 非单位缩放的判定容差。
constexpr float kScaleTolerance = 1e-3f;

// Static 体「变了没有」的判定容差。低于它就不传送 —— b3Body_SetTransform 文档标着
// 「fairly expensive」，而绝大多数静态体一辈子不动，每步传送一次纯属白烧。
constexpr float kTransformEpsilon = 1e-6f;
// 朝向的容差按弧度算：1e-4 rad 约 0.006 度，肉眼和碰撞都看不出来。
constexpr float kRotationEpsilon = 1e-4f;

// 归一化之前的最短四元数长度（的平方）。比这更短的当成「没有朝向」，退回单位四元数。
constexpr float kMinQuatLengthSquared = 1e-6f;

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
//
// 顺手归一化：`b3IsValidQuat` 要求单位四元数，而场景里的 rotation 经过几次欧拉角往返之后
// 长度会漂一点点。Debug 下那是一条 B3_ASSERT（进程直接没了），代价却只是一次开方。
// 长度为零的（默认构造出来的 glm::quat{0,0,0,0}、或者被脚本写坏的）退回单位四元数，
// 不要交给 normalize 变成 NaN。
b3Quat toBox3D(const glm::quat& value) {
    const float length_squared = glm::dot(value, value);
    if (!std::isfinite(length_squared) || length_squared < kMinQuatLengthSquared) {
        return b3Quat_identity;
    }
    const glm::quat unit = value / std::sqrt(length_squared);
    return b3Quat{ b3Vec3{ unit.x, unit.y, unit.z }, unit.w };
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

bool isFinite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool isFinite(const glm::quat& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
            std::isfinite(value.w);
}

// 两个朝向之间的最短弧夹角（弧度）。四元数的双覆盖（q 和 -q 是同一个朝向）用取绝对值处理，
// 否则「转了 0 度」会算成 2π。
float rotationDelta(const glm::quat& from, const glm::quat& to) {
    const float dot = std::clamp(std::abs(glm::dot(from, to)), 0.0f, 1.0f);
    return 2.0f * std::acos(dot);
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

// 一个实体没能参与模拟的原因。**按实体记下来去重**：body 的生命周期同步现在每个固定步都跑，
// 不去重就是每帧一条 warn 刷屏（旧实现只在建世界那一次遍历，所以不需要）。
enum class SkipReason : std::uint8_t {
    Parent,
    Scale,
    NonFiniteTransform,
    MissingCollider,
    MissingRigidBody,
};

} // namespace

struct PhysicsSystem::Impl {
    // 一个进了模拟的实体在物理世界里的身份。
    struct Body {
        b3BodyId id{ b3_nullBodyId };
        // EnTT 句柄，**含版本位**。同一个 UUID 被删掉又建出来时版本会变 —— 拿它区分「还是那个
        // 实体」和「换了一个」，不让新实体继承旧的物理身份（D4）。只比较，不解引用。
        entt::entity handle{ entt::null };
        RigidBodyComponent::Type type{ RigidBodyComponent::Type::Dynamic };
        // 上一次同步进物理世界的场景变换。Static 用它判「到底变了没有」，Kinematic 用它记
        // 「上一步的目标是什么」。
        glm::vec3 translation{ 0.0f };
        glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    };

    b3WorldId world{ b3_nullWorldId };
    // 实体 UUID → 物理身份。每个固定步重新对一遍（D4）：建新的、拆没了的、读场景拥有的。
    std::unordered_map<core::UUID, Body> bodies;
    // 已经报过的跳过原因。**同步现在每步都跑**，不去重就是每帧刷屏；原因变了要重报，
    // 所以存的是原因而不是一个 bool。
    std::unordered_map<core::UUID, SkipReason> warned_skips;
    // sync() 遍历时用的临时集合。放成员而不是局部变量：每个固定步都要一次，不想每次都分配。
    std::unordered_set<core::UUID> seen;
    // 上一次看到的帧号。帧号回退是「新会话」的兜底信号，见 requestSessionReset() 的注释。
    std::uint64_t last_frame_index{ 0 };
    // 构造完第一次同步就当新会话 —— 那时候还没有 b3World。
    bool session_reset_requested{ true };

    ~Impl() { destroyWorld(); }

    bool hasWorld() const noexcept { return !B3_IS_NULL(world); }

    void destroyWorld() {
        if (hasWorld()) {
            // 世界一拆，里面所有 body 的 id 全部失效 —— 表必须跟着清，不能留着悬空 id。
            b3DestroyWorld(world);
            world = b3_nullWorldId;
        }
        bodies.clear();
        warned_skips.clear();
    }

    // 一次新的模拟会话：旧世界连里面的 body 一起丢掉，从当前场景重新长出来。
    void beginSession() {
        destroyWorld();
        b3WorldDef world_def = b3DefaultWorldDef();
        world = b3CreateWorld(&world_def);
        session_reset_requested = false;
        getLogChannel().debug("Physics world created for a new simulation session");
    }

    Body* find(core::UUID entity) noexcept {
        const auto found = bodies.find(entity);
        return found == bodies.end() ? nullptr : &found->second;
    }

    const Body* find(core::UUID entity) const noexcept {
        const auto found = bodies.find(entity);
        return found == bodies.end() ? nullptr : &found->second;
    }

    // 速度 / 力 / 冲量的公共前置检查：body 在，而且类型是 Dynamic。
    //
    // 这里**不记日志**：返回 false 是这几个接口约定好的失败方式，而调用方是每帧跑的脚本 ——
    // 在这一层报就是每帧一条。「到底哪儿不对」由 Lua 绑定那边按 (操作, 实体) 去重地报一次，
    // 那边也才知道是缺组件、类型不对、还是被物理跳过了。
    Body* findDynamic(core::UUID entity) noexcept {
        Body* record = find(entity);
        if (record == nullptr || record->type != RigidBodyComponent::Type::Dynamic) {
            return nullptr;
        }
        return record;
    }

    void warnSkip(core::UUID id, SkipReason reason, const glm::vec3& scale);
    void clearSkipWarning(core::UUID id) { warned_skips.erase(id); }

    Body createBody(entt::entity handle, core::UUID id,
            const scene::TransformComponent& transform, const RigidBodyComponent& body,
            const ColliderComponent& collider);
    void applySceneTransform(Body& record, const scene::TransformComponent& transform, float dt);
    void applyKinematicTarget(Body& record, const scene::TransformComponent& transform, float dt);

    void sync(scene::Scene& scene, float fixed_delta_time);
    void warnOrphans(scene::Scene& scene);
    void step(scene::Scene& scene, float fixed_delta_time);
    std::optional<PhysicsSystem::RaycastHit> raycast(
            const glm::vec3& origin, const glm::vec3& translation) const;
};

void PhysicsSystem::Impl::warnSkip(core::UUID id, SkipReason reason, const glm::vec3& scale) {
    // 先查再写：insert_or_assign 之后 second 已经是新原因了，那时候比较永远相等，
    // 「原因变了要重报」就成了空话。
    const auto existing = warned_skips.find(id);
    if (existing != warned_skips.end() && existing->second == reason) {
        // 同一个实体、同一个原因，已经说过了。
        return;
    }
    warned_skips.insert_or_assign(id, reason);

    switch (reason) {
        case SkipReason::Parent:
            // D3：物理在世界空间算，而 TransformComponent 是局部的 —— 带父级的实体要拿父级的
            // 世界逆矩阵反算才能写回，那是第二个问题。静默跳过会让人以为物理坏了。
            getLogChannel().warn(
                    "Physics skips entity {}: a body with a parent is not supported yet.",
                    id.toString());
            return;
        case SkipReason::Scale:
            // 形状尺寸写在 ColliderComponent 上、不从 scale 推，所以缩放过的实体会「看起来
            // 这么大、碰撞体那么大」。
            getLogChannel().warn(
                    "Physics skips entity {}: collider sizes do not follow the Transform scale "
                    "({}, {}, {}).",
                    id.toString(), scale.x, scale.y, scale.z);
            return;
        case SkipReason::NonFiniteTransform:
            // NaN / inf 交给 Box3D 就是一条 B3_ASSERT（Debug 下进程直接没了）。
            getLogChannel().warn(
                    "Physics skips entity {}: its Transform is not finite (NaN or infinity).",
                    id.toString());
            return;
        case SkipReason::MissingCollider:
            getLogChannel().warn("Physics skips entity {}: it has a RigidBody but no Collider.",
                    id.toString());
            return;
        case SkipReason::MissingRigidBody:
            getLogChannel().warn("Physics skips entity {}: it has a Collider but no RigidBody.",
                    id.toString());
            return;
    }
}

PhysicsSystem::Impl::Body PhysicsSystem::Impl::createBody(entt::entity handle, core::UUID id,
        const scene::TransformComponent& transform, const RigidBodyComponent& body,
        const ColliderComponent& collider) {
    b3BodyDef body_def = b3DefaultBodyDef();
    body_def.type = toBox3D(body.type);
    // 没有父级（不然这个实体根本走不到这里），所以局部变换就是世界变换。
    body_def.position = toBox3D(transform.translation);
    body_def.rotation = toBox3D(transform.rotation);
    body_def.gravityScale = body.gravity_scale;
    body_def.enableSleep = body.enable_sleep;
    body_def.userData = toUserData(id);

    Body record;
    record.id = b3CreateBody(world, &body_def);
    record.handle = handle;
    record.type = body.type;
    record.translation = transform.translation;
    record.rotation = transform.rotation;
    createShape(record.id, collider);
    return record;
}

// 把场景拥有 transform 的那些 body 推到场景说的地方去。
//
// **这是 transform 所有权规则的一半**（另一半是 step() 里那个 Dynamic 判断）：
//   Dynamic          → 物理拥有 transform，写回场景
//   Static/Kinematic → 场景拥有 transform，物理只读它
//
// 没有这一步的话，脚本（或 gizmo、或将来的动画）写进 TransformComponent 的值根本到不了 body ——
// body 的位置只在建它的那一刻读过一次。症状是「视觉动了、挡人的没动、射线也打不中」。
void PhysicsSystem::Impl::applySceneTransform(
        Body& record, const scene::TransformComponent& transform, float dt) {
    switch (record.type) {
        case RigidBodyComponent::Type::Dynamic:
            // 物理拥有它。场景这边的值是上一步自己写回去的，读回来只会把求解结果绕一圈。
            return;
        case RigidBodyComponent::Type::Static:
            // 只在真的动过的时候传送。静态体绝大多数一辈子不动，而 b3Body_SetTransform 自己的
            // 文档写着「fairly expensive」—— 旧实现每步无条件传送一次，那是纯浪费。
            if (glm::length(transform.translation - record.translation) < kTransformEpsilon &&
                    rotationDelta(transform.rotation, record.rotation) < kRotationEpsilon) {
                return;
            }
            b3Body_SetTransform(
                    record.id, toBox3D(transform.translation), toBox3D(transform.rotation));
            break;
        case RigidBodyComponent::Type::Kinematic:
            applyKinematicTarget(record, transform, dt);
            break;
    }
    record.translation = transform.translation;
    record.rotation = transform.rotation;
}

// Kinematic 体：场景写的 transform 是**下一个固定步的目标**，物理据此求出线 / 角速度。
//
// 为什么不是传送（旧实现那样每步 b3Body_SetTransform）：传送不产生速度，求解器看不到「这东西
// 正在往那边走」，于是运动学体推不动 Dynamic 体 —— 电梯托不起箱子、平台带不走人，要么直接
// 穿透。给了速度，接触求解才有东西可用。
void PhysicsSystem::Impl::applyKinematicTarget(
        Body& record, const scene::TransformComponent& transform, float dt) {
    if (dt <= 0.0f) {
        // 除以它会得到 inf，而 b3Body_SetTargetTransform 在 timeStep <= 0 时本来就直接返回。
        // 这种步长下只能退回传送。
        b3Body_SetTransform(record.id, toBox3D(transform.translation), toBox3D(transform.rotation));
        return;
    }

    // 基准取**物理世界里当前的位置**，不是上一步记下的目标：求解器留下的残差会被下一步的速度
    // 自动补掉，不会一直欠着一段距离。
    const b3WorldTransform current = b3Body_GetTransform(record.id);
    const glm::vec3 offset = transform.translation - fromBox3D(current.p);
    const float angle = rotationDelta(fromBox3D(current.q), transform.rotation);
    const b3WorldTransform target{ toBox3D(transform.translation), toBox3D(transform.rotation) };

    // 目标远到求解器一步搬不过去：线速度会被世界的 maximumLinearSpeed（默认 400 m/s）截断，
    // 角速度会被 B3_MAX_ROTATION（每步 45°）截断，于是 body 落在半路上，「碰撞体就在场景说的
    // 地方」这条不变量当场破掉。这种量级的一步位移本来也推不动任何东西（接触根本来不及生成），
    // 所以按传送处理 —— 和显式 teleport 同一个道理：**复位不是高速运动**。
    //
    // 少了这一段的表现：把一个 Kinematic 体从 (1, 0.5, 0) 挪到 (-8, 0.5, -8)（12 米，一步就是
    // 722 m/s）之后，射线在新位置打不中它 —— 它要好几步才追上来。
    const float max_linear_speed = b3World_GetMaximumLinearSpeed(world);
    const float max_angular_speed = B3_MAX_ROTATION / dt;
    if (glm::length(offset) / dt > max_linear_speed || angle / dt > max_angular_speed) {
        b3Body_SetTransform(record.id, target.p, target.q);
        b3Body_SetLinearVelocity(record.id, b3Vec3_zero);
        b3Body_SetAngularVelocity(record.id, b3Vec3_zero);
        return;
    }

    b3Body_SetTargetTransform(record.id, target, dt, true);

    // **目标停下之后不会有残余速度**：对醒着的 body，SetTargetTransform 每次都把速度重写一遍，
    // 目标没变就是写 0（见 box3d 的 src/body.c）。所以这里不需要额外清零。
    //
    // 但它对**睡着的** body 有一条 early-out：隐含速度低于 sleep threshold（默认 0.05 m/s）
    // 就整个返回 —— 既不唤醒也不移动。开着休眠的平台被慢慢推时会走到这条路上（实测：一个
    // 0.03 m/s 的目标推着一个 enable_sleep 的 Kinematic 体，body 确实是睡着的），下面这一下
    // 传送把它钉在场景说的位置上。睡着的 body 不在积分、也挡不到任何正在动的东西，所以传送
    // 在这里没有「不产生推力」的代价。
    //
    // **诚实地说这不是承重的那一半**：把它删掉 physics_kinematic_smoke 照样全绿 —— 因为上面
    // 那个「基准取 body 当前位置」的写法本身就会自我纠偏（欠的距离攒到超过阈值就唤醒追上，
    // 于是变成毫米级的顿走）。留着是为了「目标动了一点点就停下」那种情况：那时候欠的距离永远
    // 攒不到阈值，没有这一下就会一直偏着。
    if (!b3Body_IsAwake(record.id) &&
            (glm::length(offset) >= kTransformEpsilon || angle >= kRotationEpsilon)) {
        b3Body_SetTransform(record.id, target.p, target.q);
    }
}

void PhysicsSystem::Impl::sync(scene::Scene& scene, float fixed_delta_time) {
    seen.clear();

    // 一次遍历同时做三件事：建新 body、拆掉不再合格的、把场景拥有的 transform 读进物理世界。
    // 合在一起是因为「这个实体够不够格参与模拟」这个判断三件事共用 —— 分开写就要写两遍，
    // 而两份判断迟早会不一致。
    for (auto [handle, id, transform, parent, body, collider]:
            scene.view<scene::IDComponent, scene::TransformComponent, scene::ParentComponent,
                            RigidBodyComponent, ColliderComponent>()
                    .each()) {
        std::optional<SkipReason> skip;
        if (parent.parent_id.isValid()) {
            skip = SkipReason::Parent;
        } else if (!isUnitScale(transform.scale)) {
            skip = SkipReason::Scale;
        } else if (!isFinite(transform.translation) || !isFinite(transform.rotation)) {
            skip = SkipReason::NonFiniteTransform;
        }
        if (skip) {
            warnSkip(id.id, *skip, transform.scale);
            // 刻意**不**放进 seen：一个原来合格的实体被挂上父级 / 缩放过 / 被写成 NaN 之后，
            // 它的 body 会在下面那一轮被拆掉。留着才是幽灵刚体。
            continue;
        }
        clearSkipWarning(id.id);
        seen.insert(id.id);

        auto found = bodies.find(id.id);
        if (found != bodies.end() &&
                (found->second.handle != handle || found->second.type != body.type)) {
            // 同一个 UUID 换了实体（删了又建），或者刚体类型变了。两种情况都不能复用旧身份：
            // 前者已经是另一个东西，后者改类型要连质量属性一起重算、Box3D 自己也标着「昂贵」。
            // **其余参数**（重力倍数、碰撞体尺寸、材质）的运行时热改仍然不在这个任务的范围内。
            b3DestroyBody(found->second.id);
            bodies.erase(found);
            found = bodies.end();
        }
        if (found == bodies.end()) {
            // 刚建出来的 body 就在场景给的位置上，这一步不用再同步一次。
            bodies.emplace(id.id, createBody(handle, id.id, transform, body, collider));
            continue;
        }
        applySceneTransform(found->second, transform, fixed_delta_time);
    }

    // 没在这次遍历里出现的 body：实体删了、必需组件删了、或者变得不合格。**必须真的
    // b3DestroyBody** —— 旧实现只是在写回时跳过它，body 还留在物理世界里挡人、还能被射线打中。
    for (auto it = bodies.begin(); it != bodies.end();) {
        if (seen.contains(it->first)) {
            ++it;
            continue;
        }
        b3DestroyBody(it->second.id);
        it = bodies.erase(it);
    }

    warnOrphans(scene);
}

// D4：两个组件都在才会被模拟。只有一个的时候不隐式补另一个（隐式创建会让「为什么这东西会挡住
// 我」变得不好查），但也不能一声不响。
void PhysicsSystem::Impl::warnOrphans(scene::Scene& scene) {
    for (auto [handle, id, body]:
            scene.view<scene::IDComponent, RigidBodyComponent>(entt::exclude<ColliderComponent>)
                    .each()) {
        (void)handle;
        (void)body;
        warnSkip(id.id, SkipReason::MissingCollider, glm::vec3{ 1.0f });
    }
    for (auto [handle, id, collider]:
            scene.view<scene::IDComponent, ColliderComponent>(entt::exclude<RigidBodyComponent>)
                    .each()) {
        (void)handle;
        (void)collider;
        warnSkip(id.id, SkipReason::MissingRigidBody, glm::vec3{ 1.0f });
    }
}

void PhysicsSystem::Impl::step(scene::Scene& scene, float fixed_delta_time) {
    if (!hasWorld()) {
        return;
    }

    b3World_Step(world, fixed_delta_time, kSubStepCount);

    // 只有这一步动过的 body 会出现在 moveEvents 里（文档明确说别每帧遍历所有 body）。这份数据
    // 到下一次 step 之前有效，所以当场消费完、不存指针。
    const b3BodyEvents events = b3World_GetBodyEvents(world);
    for (int index = 0; index < events.moveCount; ++index) {
        const b3BodyMoveEvent& move = events.moveEvents[index];
        // **只写回 Dynamic —— 这一条现在是承重的。**
        //
        // 从前（Kinematic 靠传送驱动的时候）它只是省一次无用往返：body 停在场景刚推给它的位置，
        // 写回等于把同一个值抄一遍，删掉也全绿。**接上速度驱动之后不一样了** —— 运动学体现在
        // 真的被求解器挪动，解算结果和场景给的目标之间差一个残差；写回就是拿这个残差去盖掉
        // 脚本刚写的权威值，症状又变回「按住键先卡一下」。
        if (b3Body_GetType(move.bodyId) != b3_dynamicBody) {
            continue;
        }
        auto entity = scene.findEntity(fromUserData(move.userData));
        if (!entity.isValid()) {
            // 模拟期间实体被删了（Simulate 模式下随时可能）。它的 body 会在下一次同步时被拆掉，
            // 这里只是别去写一个不存在的实体。
            continue;
        }
        auto& transform = entity.getComponent<scene::TransformComponent>();
        transform.translation = fromBox3D(move.transform.p);
        transform.rotation = fromBox3D(move.transform.q);
        // scale 不动：物理不改缩放。
    }
}

std::optional<PhysicsSystem::RaycastHit> PhysicsSystem::Impl::raycast(
        const glm::vec3& origin, const glm::vec3& translation) const {
    if (!hasWorld() || !isFinite(origin) || !isFinite(translation)) {
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

PhysicsSystem::PhysicsSystem() : m_impl(std::make_unique<Impl>()) {}

PhysicsSystem::~PhysicsSystem() = default;

void PhysicsSystem::requestSessionReset() noexcept { m_impl->session_reset_requested = true; }

void PhysicsSystem::syncBodies(scene::Scene& scene, float fixed_delta_time) {
    if (m_impl->session_reset_requested || !m_impl->hasWorld()) {
        m_impl->beginSession();
    }
    m_impl->sync(scene, fixed_delta_time);
}

void PhysicsSystem::onUpdate(scene::Scene& scene, const scene::UpdateContext& context) {
    // 「新的一次模拟会话」的兜底信号：帧号不再单调递增。真正的信号是 World::resetClock() 调
    // requestSessionReset()（D5）—— 只跑了一帧就 Stop / Play 的话两次帧号都是 0，光比大小
    // 认不出来。这里留着是为了不经过 World 直接驱动 Scene 的调用方。
    //
    // 刻意不写成 frameIndex == 0：FixedUpdate 一帧里可能被调多次（追帧），那样第一帧会重建
    // 好几次。比帧号的单调性顺带就是「这一帧已经建过了」的那道保护。
    if (context.frameIndex < m_impl->last_frame_index) {
        m_impl->session_reset_requested = true;
    }
    m_impl->last_frame_index = context.frameIndex;

    // 解算之前再同步一次。World 在脚本的固定回调之前已经调过一次了，但**这一次才是运动学目标
    // 生效的地方**：脚本刚写进 TransformComponent 的值必须在同一个固定步里进物理世界。
    syncBodies(scene, context.fixedDeltaTime);
    m_impl->step(scene, context.fixedDeltaTime);
}

std::optional<PhysicsSystem::RaycastHit> PhysicsSystem::raycast(
        const glm::vec3& origin, const glm::vec3& translation) const {
    return m_impl->raycast(origin, translation);
}

std::optional<glm::vec3> PhysicsSystem::linearVelocity(core::UUID entity) const {
    const Impl::Body* record = m_impl->find(entity);
    if (record == nullptr) {
        return std::nullopt;
    }
    // 读速度对三种类型都成立（Static 和睡着的 body 读出来是零），所以这里不筛类型 ——
    // 「这东西现在多快」是个合法的问题。
    return fromBox3D(b3Body_GetLinearVelocity(record->id));
}

bool PhysicsSystem::setLinearVelocity(core::UUID entity, const glm::vec3& velocity) {
    Impl::Body* record = m_impl->findDynamic(entity);
    if (record == nullptr || !isFinite(velocity)) {
        return false;
    }
    b3Body_SetLinearVelocity(record->id, toBox3D(velocity));
    return true;
}

bool PhysicsSystem::applyForce(core::UUID entity, const glm::vec3& force) {
    Impl::Body* record = m_impl->findDynamic(entity);
    if (record == nullptr || !isFinite(force)) {
        return false;
    }
    // 施在质心：不产生扭矩，所以「推一下」不会变成「推着转」。力会累积到下一次 b3World_Step
    // 并在那之后清掉 —— 脚本在固定回调里施的力因此在**同一个**固定步里生效。
    b3Body_ApplyForceToCenter(record->id, toBox3D(force), true);
    return true;
}

bool PhysicsSystem::applyImpulse(core::UUID entity, const glm::vec3& impulse) {
    Impl::Body* record = m_impl->findDynamic(entity);
    if (record == nullptr || !isFinite(impulse)) {
        return false;
    }
    // 冲量是**立刻**改速度（力要乘一个步长）。跳跃、爆炸这类一次性的推动用它。
    b3Body_ApplyLinearImpulseToCenter(record->id, toBox3D(impulse), true);
    return true;
}

bool PhysicsSystem::teleport(scene::Scene& scene, core::UUID entity, const glm::vec3& position) {
    if (!isFinite(position)) {
        return false;
    }
    Impl::Body* record = m_impl->find(entity);
    if (record == nullptr) {
        return false;
    }
    auto handle = scene.findEntity(entity);
    if (!handle.isValid()) {
        return false;
    }

    // 场景和物理一起改。**只改一边都不行**：Static / Kinematic 的权威值在场景那边，只挪 body
    // 会在下一次同步时被拉回去；而 Dynamic 只改场景则会被 step 之后的写回盖掉。
    auto& transform = handle.getComponent<scene::TransformComponent>();
    transform.translation = position;
    b3Body_SetTransform(record->id, toBox3D(position), toBox3D(transform.rotation));
    // 清零速度是「传送」区别于「运动」的全部：不清的话一次复位会被当成一帧走了十米，
    // 求解器要么把周围的东西弹开，要么让这个体自己继续飞。
    b3Body_SetLinearVelocity(record->id, b3Vec3_zero);
    b3Body_SetAngularVelocity(record->id, b3Vec3_zero);
    record->translation = position;
    record->rotation = transform.rotation;
    return true;
}

} // namespace arti::engine
