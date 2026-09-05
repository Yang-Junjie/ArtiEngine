// transform 所有权：谁写 TransformComponent，物理还是场景？
//
//   Dynamic          → 物理拥有，写回场景（盒子会掉）
//   Static/Kinematic → 场景拥有，物理只读（脚本 / gizmo / 动画写进去的值必须活过下一个固定步）
//
// 为什么值得单独一个测试：这条规则错了**不会崩、不会报错**，症状是「按住键，物体先卡一下才动」
// —— 脚本每帧写一点、物理每帧盖回去，直到那个 body 不再出现在 moveEvents 里才停止打架。
// 从那个现象反推到「物理和脚本在抢同一个 transform」非常远，所以在这里钉死。
//
// 这个测试**不需要脚本**：它直接在两次 tick 之间手写 TransformComponent，模拟的就是脚本
// （或 gizmo、或将来的动画系统）会做的事。

#include "physics/physics_system.h"
#include "runtime/world.h"
#include "scene/components.h"

#include "artichoco/core/log.h"
#include "artichoco/core/uuid.h"
#include "artichoco/scene/components.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <glm/vec3.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace arti;

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "physics_transform_ownership_smoke: " << message << '\n';
    }
    return condition;
}

constexpr float kStep = 1.0f / 60.0f;

glm::vec3 translationOf(scene::Scene& scene, core::UUID id) {
    return scene.findEntity(id).getComponent<scene::TransformComponent>().translation;
}

void setTranslation(scene::Scene& scene, core::UUID id, const glm::vec3& value) {
    scene.findEntity(id).getComponent<scene::TransformComponent>().translation = value;
}

// 一个带碰撞体的实体。type 决定所有权。
core::UUID makeBody(scene::Scene& scene, const char* name, engine::RigidBodyComponent::Type type,
        const glm::vec3& position, const glm::vec3& half_extents) {
    auto entity = scene.createEntity(name);
    entity.getComponent<scene::TransformComponent>().translation = position;
    auto& body = entity.addComponent<engine::RigidBodyComponent>();
    body.type = type;
    body.enable_sleep = false;
    auto& collider = entity.addComponent<engine::ColliderComponent>();
    collider.shape = engine::ColliderComponent::Shape::Box;
    collider.half_extents = half_extents;
    return entity.getUUID();
}

int run() {
    engine::World world;
    auto& scene = world.scene();

    // 地面（Static）+ 一个脚本驱动的方块（Kinematic）+ 一个会掉的盒子（Dynamic）。
    const core::UUID ground = makeBody(scene, "Ground",
            engine::RigidBodyComponent::Type::Static, glm::vec3{ 0.0f, -0.25f, 0.0f },
            glm::vec3{ 20.0f, 0.25f, 20.0f });
    const core::UUID mover = makeBody(scene, "Kinematic Mover",
            engine::RigidBodyComponent::Type::Kinematic, glm::vec3{ 0.0f, 0.5f, 0.0f },
            glm::vec3{ 0.5f, 0.5f, 0.5f });
    const core::UUID faller = makeBody(scene, "Dynamic Faller",
            engine::RigidBodyComponent::Type::Dynamic, glm::vec3{ 5.0f, 6.0f, 0.0f },
            glm::vec3{ 0.5f, 0.5f, 0.5f });

    // 建世界 + 跑几帧，让物理进入稳定状态（这样后面失败就不能怪「还没热起来」）。
    for (int frame = 0; frame < 5; ++frame) {
        world.tick(kStep);
    }

    // ---- Kinematic：场景写进去的值必须活过下一个固定步 ----
    //
    // 这是这个测试的**主断言**。所有权规则错了的话，物理会把 transform 盖回 body 的老位置，
    // 于是每帧只剩「这一帧刚加的那一点」，累加不起来。
    constexpr float kNudge = 0.1f;
    constexpr int kNudgeFrames = 10;
    const float start_x = translationOf(scene, mover).x;
    for (int frame = 0; frame < kNudgeFrames; ++frame) {
        // 模拟脚本：读一份、加一点、写回去。
        auto position = translationOf(scene, mover);
        position.x += kNudge;
        setTranslation(scene, mover, position);
        world.tick(kStep);
    }

    const float expected_x = start_x + kNudge * static_cast<float>(kNudgeFrames);
    const float actual_x = translationOf(scene, mover).x;
    if (!require(std::fabs(actual_x - expected_x) < 1e-4f,
                "Kinematic 体的位移被物理盖掉了：x=" + std::to_string(actual_x) + "，应该是 " +
                        std::to_string(expected_x) +
                        "（物理不该写回非 Dynamic 的 transform）")) {
        return 1;
    }

    // Y 也不许被动：Kinematic 不受重力，而且它的 transform 归场景。
    if (!require(std::fabs(translationOf(scene, mover).y - 0.5f) < 1e-4f,
                "Kinematic 体的 y 被物理改了（y=" +
                        std::to_string(translationOf(scene, mover).y) + "）")) {
        return 1;
    }

    // ---- Static：同样归场景 ----
    setTranslation(scene, ground, glm::vec3{ 1.0f, -0.25f, 2.0f });
    world.tick(kStep);
    const auto ground_position = translationOf(scene, ground);
    if (!require(std::fabs(ground_position.x - 1.0f) < 1e-4f &&
                        std::fabs(ground_position.z - 2.0f) < 1e-4f,
                "Static 体挪不动 —— 物理把它的 transform 盖回去了")) {
        return 1;
    }

    // ---- Dynamic：反过来，物理必须还在写回 ----
    //
    // 这条是上面那个改动的对照组：只写回 Dynamic **不能**变成「谁都不写回」，
    // 否则盒子就不会掉了，而那才是物理最基本的功能。
    const float faller_start_y = translationOf(scene, faller).y;
    for (int frame = 0; frame < 30; ++frame) {
        world.tick(kStep);
    }
    const float faller_y = translationOf(scene, faller).y;
    if (!require(faller_y < faller_start_y - 0.05f,
                "Dynamic 体没有下落（y 从 " + std::to_string(faller_start_y) + " 到 " +
                        std::to_string(faller_y) + "）—— 写回被砍多了")) {
        return 1;
    }

    // 而且它最终该停在地面上，不该穿过去。地面上表面在 y=0，半长 0.5 的盒子中心停在 0.5。
    for (int frame = 0; frame < 240; ++frame) {
        world.tick(kStep);
    }
    const float resting_y = translationOf(scene, faller).y;
    if (!require(resting_y > 0.3f && resting_y < 0.7f,
                "Dynamic 体没停在地面上（y=" + std::to_string(resting_y) +
                        "）—— Static 地面同步之后碰撞是否还成立？")) {
        return 1;
    }

    // ---- 碰撞体必须真的跟着走（这一条抓的是另一半：syncSceneOwned）----
    //
    // 上面那些断言只证明了「transform 没被物理盖掉」。但那还不够 —— 如果物理只是「不写回」、
    // 却也不去**读**场景，body 就冻在 buildWorld() 那一刻的位置：视觉上平台动了，挡人的东西
    // 没动，射线也打不中它。这是脚本推平台 / 推角色时最容易出的那种「看得见但摸不着」。
    //
    // 所以拿射线当探针：把 Kinematic 方块挪到一个远处的位置，那里必须能打中它，而它原来的
    // 位置必须打不中了。射线走的是物理世界，所以它问的正是「body 在哪」。
    {
        constexpr glm::vec3 kFarAway{ -8.0f, 0.5f, -8.0f };
        const auto before = translationOf(scene, mover);
        setTranslation(scene, mover, kFarAway);
        world.tick(kStep);

        auto& physics = scene.getSystem<engine::PhysicsSystem>();
        const auto down = glm::vec3{ 0.0f, -4.0f, 0.0f };

        const auto at_new = physics.raycast(kFarAway + glm::vec3{ 0.0f, 3.0f, 0.0f }, down);
        if (!require(at_new.has_value() && at_new->entity == mover,
                    "把 Kinematic 体挪走之后，新位置打不中它 —— 碰撞体没跟着走"
                    "（物理没有读场景的 transform）")) {
            return 1;
        }

        // 原来的位置不该还有它。地面在那儿，所以打中的应该是地面而不是方块。
        const auto at_old = physics.raycast(
                glm::vec3{ before.x, before.y + 3.0f, before.z }, down);
        if (!require(!at_old.has_value() || at_old->entity != mover,
                    "Kinematic 体挪走了，但原来的位置还打得中它 —— 物理世界里留了个影子")) {
            return 1;
        }
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
