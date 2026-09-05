// 引擎侧射线查询冒烟：用 World 摆一个地面和一个会掉的盒子，tick 几帧让盒子落地，
// 再从上方打一枪，命中必须是场景里那个实体。
//
// 和 physics_smoke 的分工：那个只链 box3d、钉住库本身的 API；这个链 Runtime，钉住
// PhysicsSystem::raycast 把 b3* 翻成引擎类型（UUID / glm）这一层。Lua 绑定吃的就是这个。

#include "physics/physics_system.h"
#include "runtime/world.h"
#include "scene/components.h"

#include "artichoco/core/log.h"
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
        std::cerr << "physics_raycast_smoke: " << message << '\n';
    }
    return condition;
}

int run() {
    engine::World world;
    auto& scene = world.scene();

    // 还没 tick 过：物理世界不存在，射线必须返回空，不能崩。
    if (!require(!world.scene().getSystem<engine::PhysicsSystem>().raycast(
                         glm::vec3{ 0.0f, 10.0f, 0.0f }, glm::vec3{ 0.0f, -20.0f, 0.0f })
                         .has_value(),
                "没建物理世界时 raycast 居然命中了")) {
        return 1;
    }

    auto ground = scene.createEntity("Ground");
    ground.getComponent<scene::TransformComponent>().translation = glm::vec3{ 0.0f, -0.5f, 0.0f };
    auto& ground_body = ground.addComponent<engine::RigidBodyComponent>();
    ground_body.type = engine::RigidBodyComponent::Type::Static;
    auto& ground_collider = ground.addComponent<engine::ColliderComponent>();
    ground_collider.shape = engine::ColliderComponent::Shape::Box;
    ground_collider.half_extents = glm::vec3{ 50.0f, 0.5f, 50.0f };
    const core::UUID ground_id = ground.getUUID();

    auto box = scene.createEntity("Box");
    box.getComponent<scene::TransformComponent>().translation = glm::vec3{ 0.0f, 4.0f, 0.0f };
    box.addComponent<engine::RigidBodyComponent>();
    auto& box_collider = box.addComponent<engine::ColliderComponent>();
    box_collider.shape = engine::ColliderComponent::Shape::Box;
    box_collider.half_extents = glm::vec3{ 0.5f, 0.5f, 0.5f };
    const core::UUID box_id = box.getUUID();

    // 1 秒足够让盒子从 y=4 掉到地面上（重力 10、无初速，落地大约 0.9s）。
    for (int step = 0; step < 60; ++step) {
        world.tick(1.0f / 60.0f);
    }

    auto& physics = scene.getSystem<engine::PhysicsSystem>();

    // 从正上方往下打：应该打中盒子顶面（落地后中心约 y=0.5，顶面 y=1）。
    const auto hit = physics.raycast(glm::vec3{ 0.0f, 10.0f, 0.0f }, glm::vec3{ 0.0f, -20.0f, 0.0f });
    if (!require(hit.has_value(), "落地后从上方打一枪什么都没打中")) {
        return 1;
    }
    if (!require(hit->entity == box_id, "打中的不是那个盒子（UUID=" + hit->entity.toString() + "）")) {
        return 1;
    }
    if (!require(std::fabs(hit->point.y - 1.0f) < 0.15f,
                "命中点不在盒子顶面附近（y=" + std::to_string(hit->point.y) + "）")) {
        return 1;
    }
    if (!require(hit->normal.y > 0.5f, "命中法线不是朝上的")) {
        return 1;
    }
    if (!require(hit->fraction > 0.0f && hit->fraction < 1.0f, "fraction 不在 (0, 1) 里")) {
        return 1;
    }

    // 从盒子旁边往下打，应该打中地面（上表面 y=0）。
    const auto ground_hit =
            physics.raycast(glm::vec3{ 5.0f, 10.0f, 0.0f }, glm::vec3{ 0.0f, -20.0f, 0.0f });
    if (!require(ground_hit.has_value() && ground_hit->entity == ground_id,
                "旁边往下打没有打中地面")) {
        return 1;
    }

    // 完全打空。
    if (!require(!physics.raycast(glm::vec3{ 0.0f, 10.0f, 0.0f }, glm::vec3{ 0.0f, 5.0f, 0.0f })
                         .has_value(),
                "朝天上打居然命中了")) {
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
