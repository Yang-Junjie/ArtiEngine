// 运动学体的连续运动、刚体控制接口、以及 body 的生命周期同步。
//
// 这个文件钉的是 2026-09-05 那一轮桥接里三件**从现象反推很远**的事：
//
//   1. Kinematic 体是用「目标 → 速度」驱动的，不是每步传送。传送不产生速度，求解器看不到
//      「这东西正在往上走」，于是电梯托不起箱子、平台带不走人 —— 而画面上平台明明在动。
//      主断言用**被托着的盒子的速度**来分辨这两种实现：位置重叠推出来的那种被
//      contactSpeed（默认 3 m/s）截着，给了速度的那种能跟上 6 m/s。
//   2. body 的增删每个固定步同步一次。旧实现只在会话开始时建一次世界，删掉实体只是「不写回
//      transform」—— body 还留在物理世界里挡人、还能被射线打中，也就是幽灵刚体。
//   3. 控制接口对无效输入必须**返回失败**而不是踩 Box3D 的 B3_ASSERT：调用方是用户写的 Lua。
//
// 射线在这里当探针用：它问的是「body 到底在哪」，而断言 TransformComponent 只能问「场景以为
// 它在哪」。两者不是一件事，这条教训是上一轮 physics_transform_ownership_smoke 换来的。

#include "physics/physics_system.h"
#include "runtime/world.h"
#include "scene/components.h"

#include "artichoco/core/log.h"
#include "artichoco/core/uuid.h"
#include "artichoco/scene/components.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

using namespace arti;

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "physics_kinematic_smoke: " << message << '\n';
    }
    return condition;
}

constexpr float kStep = 1.0f / 60.0f;

// 一个带碰撞体的盒子。type 决定 transform 归谁（见 Scene.md 3.1.1）。
core::UUID makeBox(scene::Scene& scene, const char* name, engine::RigidBodyComponent::Type type,
        const glm::vec3& position, const glm::vec3& half_extents, bool enable_sleep = false) {
    auto entity = scene.createEntity(name);
    entity.getComponent<scene::TransformComponent>().translation = position;
    auto& body = entity.addComponent<engine::RigidBodyComponent>();
    body.type = type;
    body.enable_sleep = enable_sleep;
    auto& collider = entity.addComponent<engine::ColliderComponent>();
    collider.shape = engine::ColliderComponent::Shape::Box;
    collider.half_extents = half_extents;
    return entity.getUUID();
}

glm::vec3 translationOf(scene::Scene& scene, core::UUID id) {
    return scene.findEntity(id).getComponent<scene::TransformComponent>().translation;
}

void setTranslation(scene::Scene& scene, core::UUID id, const glm::vec3& value) {
    scene.findEntity(id).getComponent<scene::TransformComponent>().translation = value;
}

void tick(engine::World& world, int frames) {
    for (int frame = 0; frame < frames; ++frame) {
        world.tick(kStep);
    }
}

// 从 (x, height, z) 往下打 depth 米，返回命中的实体（没打中返回无效 UUID）。
core::UUID probeDown(engine::PhysicsSystem& physics, const glm::vec3& from, float depth) {
    const auto hit = physics.raycast(from, glm::vec3{ 0.0f, -depth, 0.0f });
    return hit ? hit->entity : core::UUID{};
}

// ---- 1. 运动学平台托起动态盒，停下之后谁都不漂 ----
bool checkPlatformLift() {
    engine::World world;
    auto& scene = world.scene();

    makeBox(scene, "Ground", engine::RigidBodyComponent::Type::Static,
            glm::vec3{ 0.0f, -2.25f, 0.0f }, glm::vec3{ 20.0f, 0.25f, 20.0f });
    // 平台上表面在 y = 0.25，盒子（半长 0.5）因此停在 y = 0.75。
    const core::UUID platform = makeBox(scene, "Platform",
            engine::RigidBodyComponent::Type::Kinematic, glm::vec3{ 0.0f, 0.0f, 0.0f },
            glm::vec3{ 2.0f, 0.25f, 2.0f });
    const core::UUID rider = makeBox(scene, "Rider", engine::RigidBodyComponent::Type::Dynamic,
            glm::vec3{ 0.0f, 0.9f, 0.0f }, glm::vec3{ 0.5f, 0.5f, 0.5f });

    // 先落稳，这样后面失败不能怪「还没接触上」。
    tick(world, 60);
    auto& physics = scene.getSystem<engine::PhysicsSystem>();
    if (!require(std::fabs(translationOf(scene, rider).y - 0.75f) < 0.05f,
                "盒子没停在平台上（y=" + std::to_string(translationOf(scene, rider).y) +
                        "，应该约 0.75）")) {
        return false;
    }

    // 平台以 6 m/s 上升半秒。6 刻意选在 contactSpeed（默认 3 m/s，位置重叠的推出速度上限）
    // 之上 —— 这就是「给了速度」和「靠重叠推」的分界线。
    constexpr float kLiftSpeed = 6.0f;
    constexpr int kLiftFrames = 30;
    const float rider_start_y = translationOf(scene, rider).y;
    for (int frame = 0; frame < kLiftFrames; ++frame) {
        auto position = translationOf(scene, platform);
        position.y += kLiftSpeed * kStep;
        setTranslation(scene, platform, position);
        world.tick(kStep);
    }

    const float lifted = kLiftSpeed * kStep * static_cast<float>(kLiftFrames);
    const float rider_y = translationOf(scene, rider).y;
    if (!require(rider_y > rider_start_y + lifted * 0.8f,
                "盒子没被平台托起来（y 从 " + std::to_string(rider_start_y) + " 到 " +
                        std::to_string(rider_y) + "，平台走了 " + std::to_string(lifted) +
                        "）—— 运动学体是不是还在用传送驱动？")) {
        return false;
    }

    // **主断言**：盒子自己有速度。传送驱动的平台只能靠重叠把它推出去，那个速度被
    // contactSpeed 截在 3 m/s 附近，跟不上 6。
    const auto rider_velocity = physics.linearVelocity(rider);
    if (!require(rider_velocity.has_value(), "读不到盒子的线速度")) {
        return false;
    }
    if (!require(rider_velocity->y > 4.0f,
                "盒子是被推出去的，不是被带着走的（vy=" + std::to_string(rider_velocity->y) +
                        "，平台是 " + std::to_string(kLiftSpeed) + "）")) {
        return false;
    }

    // 目标停下：平台不许继续漂。射线打在平台露出来的那一角（盒子只盖住中间 ±0.5）。
    const float resting_y = translationOf(scene, platform).y;
    tick(world, 120);
    const auto edge = physics.raycast(
            glm::vec3{ 1.5f, resting_y + 3.0f, 0.0f }, glm::vec3{ 0.0f, -6.0f, 0.0f });
    if (!require(edge.has_value() && edge->entity == platform,
                "停下之后射线打不到平台了 —— body 漂走了")) {
        return false;
    }
    if (!require(std::fabs(edge->point.y - (resting_y + 0.25f)) < 0.02f,
                "平台停下之后仍在漂（上表面在 " + std::to_string(edge->point.y) + "，应该是 " +
                        std::to_string(resting_y + 0.25f) + "）—— 残余速度没清掉")) {
        return false;
    }
    return true;
}

// ---- 2. 比 sleep threshold 还慢的目标，碰撞体也得跟上 ----
//
// b3Body_SetTargetTransform 对**睡着的** body 有一条 early-out：隐含速度低于 sleepThreshold
// （默认 0.05 m/s）就整个返回，既不唤醒也不移动。开着休眠的平台被慢慢推的时候正好走到这条路上
// （实测过：body 真的是睡着的），而症状在画面上完全看不出来 —— 只有「怎么走不上去」。
//
// 这一节是**行为保证**，不对应某一行实现：目标 → 速度那条路和「睡着就补一下传送」那条路各自
// 都能让它绿（去掉后者仍然绿，实测）。它挡的是「有人把慢速目标整个丢掉」那一类改动。
bool checkSlowTarget() {
    engine::World world;
    auto& scene = world.scene();

    // 悬空放，不跟任何东西接触 —— 这样它一定会睡着（有接触也能睡，但没接触更干脆）。
    // **enable_sleep 是 true**：这一条测的就是睡着之后那条路。
    const core::UUID platform = makeBox(scene, "Sleepy Platform",
            engine::RigidBodyComponent::Type::Kinematic, glm::vec3{ 0.0f, 4.0f, 0.0f },
            glm::vec3{ 0.25f, 0.25f, 0.25f }, /*enable_sleep=*/true);

    // 两秒不动，足够超过 Box3D 的 timeToSleep。
    tick(world, 120);

    // 每步 0.5 毫米 = 0.03 m/s，稳稳低于 0.05 的阈值。1200 步刚好走 0.6 米，比自己的边长还长，
    // 所以「原来的位置」和「新位置」不会重叠。
    constexpr float kCrawl = 0.0005f;
    constexpr int kCrawlFrames = 1200;
    for (int frame = 0; frame < kCrawlFrames; ++frame) {
        auto position = translationOf(scene, platform);
        position.x += kCrawl;
        setTranslation(scene, platform, position);
        world.tick(kStep);
    }

    auto& physics = scene.getSystem<engine::PhysicsSystem>();
    const glm::vec3 now = translationOf(scene, platform);
    if (!require(probeDown(physics, now + glm::vec3{ 0.0f, 2.0f, 0.0f }, 4.0f) == platform,
                "慢慢推了 " + std::to_string(kCrawl * kCrawlFrames) +
                        " 米之后，新位置打不中平台 —— 睡着的 body 把小目标丢了")) {
        return false;
    }
    if (!require(probeDown(physics, glm::vec3{ 0.0f, 6.0f, 0.0f }, 4.0f) != platform,
                "平台挪走了，原来的位置还打得中它 —— 物理世界里留了个影子")) {
        return false;
    }
    return true;
}

// ---- 3. 旋转的目标 ----
bool checkRotatingTarget() {
    engine::World world;
    auto& scene = world.scene();

    // 一根沿 X 躺着的长条：±2 长、±0.25 厚。转 90° 之后它会横到 Z 上。
    const core::UUID beam = makeBox(scene, "Beam", engine::RigidBodyComponent::Type::Kinematic,
            glm::vec3{ 0.0f, 2.0f, 0.0f }, glm::vec3{ 2.0f, 0.25f, 0.25f });
    tick(world, 5);

    auto& physics = scene.getSystem<engine::PhysicsSystem>();
    const glm::vec3 off_axis{ 0.0f, 5.0f, 1.5f };
    if (!require(probeDown(physics, off_axis, 6.0f) != beam,
                "还没转，z=1.5 处就已经打得中长条了 —— 这个探针选错了位置")) {
        return false;
    }

    // 每步 1°，共 90 步。远低于 B3_MAX_ROTATION（每步 45°）那道截断。
    for (int frame = 0; frame < 90; ++frame) {
        auto& transform = scene.findEntity(beam).getComponent<scene::TransformComponent>();
        transform.rotation = glm::angleAxis(glm::radians(static_cast<float>(frame + 1)),
                glm::vec3{ 0.0f, 1.0f, 0.0f });
        world.tick(kStep);
    }

    if (!require(probeDown(physics, off_axis, 6.0f) == beam,
                "转了 90° 之后 z=1.5 处打不中长条 —— 碰撞体的朝向没跟着走")) {
        return false;
    }
    // 场景写的朝向不许被物理盖掉（Kinematic 的 transform 归场景）。
    const glm::quat expected = glm::angleAxis(glm::radians(90.0f), glm::vec3{ 0.0f, 1.0f, 0.0f });
    const glm::quat actual =
            scene.findEntity(beam).getComponent<scene::TransformComponent>().rotation;
    if (!require(std::fabs(std::abs(glm::dot(expected, actual)) - 1.0f) < 1e-4f,
                "Kinematic 体的朝向被物理写回盖掉了")) {
        return false;
    }
    return true;
}

// ---- 4. 动态刚体的控制接口 ----
bool checkDynamicControls() {
    engine::World world;
    auto& scene = world.scene();

    const core::UUID ground = makeBox(scene, "Ground", engine::RigidBodyComponent::Type::Static,
            glm::vec3{ 0.0f, -0.25f, 0.0f }, glm::vec3{ 20.0f, 0.25f, 20.0f });
    const core::UUID box = makeBox(scene, "Box", engine::RigidBodyComponent::Type::Dynamic,
            glm::vec3{ 0.0f, 0.5f, 0.0f }, glm::vec3{ 0.5f, 0.5f, 0.5f });
    tick(world, 60);
    auto& physics = scene.getSystem<engine::PhysicsSystem>();

    // 读速度：落稳之后接近零。
    const auto resting = physics.linearVelocity(box);
    if (!require(resting.has_value() && glm::length(*resting) < 0.1f,
                "落稳之后读到的线速度不接近零")) {
        return false;
    }

    // 写速度。
    if (!require(physics.setLinearVelocity(box, glm::vec3{ 3.0f, 0.0f, 0.0f }),
                "set_linear_velocity 在 Dynamic 体上失败了")) {
        return false;
    }
    const float before_x = translationOf(scene, box).x;
    tick(world, 10);
    if (!require(translationOf(scene, box).x > before_x + 0.2f,
                "设了速度之后盒子没动（x 从 " + std::to_string(before_x) + " 到 " +
                        std::to_string(translationOf(scene, box).x) + "）")) {
        return false;
    }

    // 冲量：**立刻**改速度，不用等一步。质量 = 密度 1 × 体积 1 = 1 kg，所以 5 N·s 就是 5 m/s。
    if (!require(physics.applyImpulse(box, glm::vec3{ 0.0f, 5.0f, 0.0f }), "apply_impulse 失败")) {
        return false;
    }
    const auto kicked = physics.linearVelocity(box);
    if (!require(kicked.has_value() && kicked->y > 3.0f,
                "冲量没有立刻改速度（vy=" +
                        std::to_string(kicked ? kicked->y : 0.0f) + "）")) {
        return false;
    }

    // 力：累积到下一次 step。100 N 顶 1 kg，减掉 10 的重力还剩 90 m/s²。
    physics.teleport(scene, box, glm::vec3{ 0.0f, 0.5f, 0.0f });
    const float lift_start_y = translationOf(scene, box).y;
    for (int frame = 0; frame < 30; ++frame) {
        if (!require(physics.applyForce(box, glm::vec3{ 0.0f, 100.0f, 0.0f }), "apply_force 失败")) {
            return false;
        }
        world.tick(kStep);
    }
    if (!require(translationOf(scene, box).y > lift_start_y + 1.0f,
                "持续施力没能把盒子推起来（y 从 " + std::to_string(lift_start_y) + " 到 " +
                        std::to_string(translationOf(scene, box).y) + "）")) {
        return false;
    }

    // 传送：场景和物理一起改，速度清零。
    constexpr glm::vec3 kTarget{ 5.0f, 3.0f, -4.0f };
    if (!require(physics.teleport(scene, box, kTarget), "teleport 失败")) {
        return false;
    }
    if (!require(glm::length(translationOf(scene, box) - kTarget) < 1e-4f,
                "teleport 没改场景里的 translation")) {
        return false;
    }
    const auto after_teleport = physics.linearVelocity(box);
    if (!require(after_teleport.has_value() && glm::length(*after_teleport) < 1e-4f,
                "teleport 之后线速度没清零 —— 一次复位会被当成高速运动")) {
        return false;
    }
    if (!require(probeDown(physics, kTarget + glm::vec3{ 0.0f, 3.0f, 0.0f }, 6.0f) == box,
                "teleport 之后新位置打不中盒子 —— body 没跟着走")) {
        return false;
    }

    // ---- 失败路径：一个都不许踩 Box3D 的断言 ----
    if (!require(!physics.setLinearVelocity(ground, glm::vec3{ 1.0f, 0.0f, 0.0f }) &&
                        !physics.applyForce(ground, glm::vec3{ 0.0f, 1.0f, 0.0f }) &&
                        !physics.applyImpulse(ground, glm::vec3{ 0.0f, 1.0f, 0.0f }),
                "速度 / 力 / 冲量在 Static 体上居然成功了")) {
        return false;
    }
    const core::UUID nobody = core::UUID::generate();
    if (!require(!physics.linearVelocity(nobody).has_value() &&
                        !physics.setLinearVelocity(nobody, glm::vec3{ 1.0f }) &&
                        !physics.applyForce(nobody, glm::vec3{ 1.0f }) &&
                        !physics.applyImpulse(nobody, glm::vec3{ 1.0f }) &&
                        !physics.teleport(scene, nobody, glm::vec3{ 1.0f }),
                "不存在的实体上的控制调用居然成功了")) {
        return false;
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    if (!require(!physics.setLinearVelocity(box, glm::vec3{ nan, 0.0f, 0.0f }) &&
                        !physics.applyForce(box, glm::vec3{ 0.0f, inf, 0.0f }) &&
                        !physics.applyImpulse(box, glm::vec3{ 0.0f, 0.0f, nan }) &&
                        !physics.teleport(scene, box, glm::vec3{ inf, 0.0f, 0.0f }),
                "非有限的输入居然被接受了")) {
        return false;
    }
    // 非法输入之后世界还得能跑。
    tick(world, 10);
    return true;
}

// ---- 5. body 的生命周期：运行中增删实体 / 组件 ----
//
// 旧实现只在会话开始时建一次世界，所以这一整节全是红的：新实体不会掉、删掉的实体留下幽灵刚体。
bool checkBodyLifecycle() {
    engine::World world;
    auto& scene = world.scene();

    makeBox(scene, "Ground", engine::RigidBodyComponent::Type::Static,
            glm::vec3{ 0.0f, -0.25f, 0.0f }, glm::vec3{ 40.0f, 0.25f, 40.0f });
    const core::UUID doomed = makeBox(scene, "Doomed", engine::RigidBodyComponent::Type::Static,
            glm::vec3{ 6.0f, 0.5f, 0.0f }, glm::vec3{ 0.5f, 0.5f, 0.5f });
    const core::UUID stripped = makeBox(scene, "Stripped", engine::RigidBodyComponent::Type::Static,
            glm::vec3{ 9.0f, 0.5f, 0.0f }, glm::vec3{ 0.5f, 0.5f, 0.5f });
    const core::UUID adopted = makeBox(scene, "Adopted", engine::RigidBodyComponent::Type::Static,
            glm::vec3{ 12.0f, 0.5f, 0.0f }, glm::vec3{ 0.5f, 0.5f, 0.5f });
    tick(world, 5);
    auto& physics = scene.getSystem<engine::PhysicsSystem>();

    // 运行中新加的实体要能参与模拟。
    const core::UUID latecomer = makeBox(scene, "Latecomer",
            engine::RigidBodyComponent::Type::Dynamic, glm::vec3{ 0.0f, 5.0f, 0.0f },
            glm::vec3{ 0.5f, 0.5f, 0.5f });
    tick(world, 90);
    if (!require(translationOf(scene, latecomer).y < 1.0f,
                "模拟中新建的实体没有下落（y=" +
                        std::to_string(translationOf(scene, latecomer).y) +
                        "）—— 它没被建成 body")) {
        return false;
    }

    // 删实体 → body 必须真的没了，不能留个影子挡人 / 挡射线。
    if (!require(probeDown(physics, glm::vec3{ 6.0f, 3.0f, 0.0f }, 6.0f) == doomed,
                "删之前就打不中 Doomed，探针位置不对")) {
        return false;
    }
    scene.destroyEntity(scene.findEntity(doomed));
    world.tick(kStep);
    if (!require(probeDown(physics, glm::vec3{ 6.0f, 3.0f, 0.0f }, 6.0f) != doomed,
                "实体删了，射线还打得中它 —— 幽灵刚体")) {
        return false;
    }

    // 摘掉 Collider → 同样不再参与模拟。
    scene.findEntity(stripped).removeComponent<engine::ColliderComponent>();
    world.tick(kStep);
    if (!require(probeDown(physics, glm::vec3{ 9.0f, 3.0f, 0.0f }, 6.0f) != stripped,
                "Collider 摘掉了，射线还打得中它")) {
        return false;
    }

    // 挂上父级 → 物理跳过它（世界空间那条限制），body 也要拆掉。
    auto parent = scene.createEntity("Parent");
    scene.setParent(scene.findEntity(adopted), parent);
    world.tick(kStep);
    if (!require(probeDown(physics, glm::vec3{ 12.0f, 3.0f, 0.0f }, 6.0f) != adopted,
                "挂上父级之后射线还打得中它 —— 不合格的 body 没被拆掉")) {
        return false;
    }

    // 刚体类型换掉 → 重建。Dynamic 换成 Static 之后不该再掉。
    auto& latecomer_body = scene.findEntity(latecomer).getComponent<engine::RigidBodyComponent>();
    latecomer_body.type = engine::RigidBodyComponent::Type::Static;
    setTranslation(scene, latecomer, glm::vec3{ 0.0f, 4.0f, 0.0f });
    tick(world, 60);
    if (!require(std::fabs(translationOf(scene, latecomer).y - 4.0f) < 1e-3f,
                "换成 Static 之后还在掉（y=" +
                        std::to_string(translationOf(scene, latecomer).y) + "）")) {
        return false;
    }

    // 同一个 UUID 删了又建：新实体不能继承旧的物理身份。
    const core::UUID reused = core::UUID::generate();
    {
        auto entity = scene.createEntityWithUUID(reused, "Reused A");
        entity.getComponent<scene::TransformComponent>().translation =
                glm::vec3{ -6.0f, 0.5f, 0.0f };
        auto& body = entity.addComponent<engine::RigidBodyComponent>();
        body.type = engine::RigidBodyComponent::Type::Static;
        body.enable_sleep = false;
        auto& collider = entity.addComponent<engine::ColliderComponent>();
        collider.half_extents = glm::vec3{ 0.5f, 0.5f, 0.5f };
    }
    world.tick(kStep);
    scene.destroyEntity(scene.findEntity(reused));
    {
        auto entity = scene.createEntityWithUUID(reused, "Reused B");
        entity.getComponent<scene::TransformComponent>().translation =
                glm::vec3{ -12.0f, 0.5f, 0.0f };
        auto& body = entity.addComponent<engine::RigidBodyComponent>();
        body.type = engine::RigidBodyComponent::Type::Static;
        body.enable_sleep = false;
        auto& collider = entity.addComponent<engine::ColliderComponent>();
        collider.half_extents = glm::vec3{ 0.5f, 0.5f, 0.5f };
    }
    world.tick(kStep);
    if (!require(probeDown(physics, glm::vec3{ -12.0f, 3.0f, 0.0f }, 6.0f) == reused,
                "同 UUID 重建之后，新位置打不中它 —— 复用了旧的物理身份")) {
        return false;
    }
    if (!require(probeDown(physics, glm::vec3{ -6.0f, 3.0f, 0.0f }, 6.0f) != reused,
                "同 UUID 重建之后，旧位置还打得中它 —— 旧 body 没拆")) {
        return false;
    }
    return true;
}

// ---- 6. 会话重置：只跑一帧就重来，物理世界也得是新的 ----
//
// 光靠「帧号回退」认不出这一种：两次都是 frameIndex == 0，`0 < 0` 不成立。所以 World::resetClock
// 显式通知（D5）。反向验证的做法是把那两句 requestSessionReset 去掉 —— 这一节会红。
bool checkSessionReset() {
    engine::World world;
    auto& scene = world.scene();

    const core::UUID box = makeBox(scene, "Box", engine::RigidBodyComponent::Type::Dynamic,
            glm::vec3{ 0.0f, 5.0f, 0.0f }, glm::vec3{ 0.5f, 0.5f, 0.5f });
    // **只跑一帧。**
    world.tick(kStep);

    // 「Stop 之后再 Play」：场景被恢复到编辑时的样子，时钟归零。
    setTranslation(scene, box, glm::vec3{ 20.0f, 5.0f, 0.0f });
    world.resetClock();
    world.tick(kStep);

    auto& physics = scene.getSystem<engine::PhysicsSystem>();
    if (!require(probeDown(physics, glm::vec3{ 20.0f, 8.0f, 0.0f }, 6.0f) == box,
                "重开会话之后 body 还在老地方 —— 物理世界没重建")) {
        return false;
    }
    return true;
}

int run() {
    if (!checkPlatformLift()) {
        return 1;
    }
    if (!checkSlowTarget()) {
        return 1;
    }
    if (!checkRotatingTarget()) {
        return 1;
    }
    if (!checkDynamicControls()) {
        return 1;
    }
    if (!checkBodyLifecycle()) {
        return 1;
    }
    if (!checkSessionReset()) {
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    arti::core::Logger::init();
    const int result = run();
    arti::core::Logger::shutdown();
    return result;
}
