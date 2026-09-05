#pragma once
#include "artichoco/core/uuid.h"
#include "artichoco/scene/system.h"

#include <glm/vec3.hpp>

#include <memory>
#include <optional>

namespace arti::scene {
class Scene;
}

namespace arti::engine {

// 刚体物理：把场景里带 RigidBody + Collider 的实体变成一个 Box3D 世界，每个固定步长推进一次，
// 再把结果写回 TransformComponent。
//
// 注册在 World 的构造函数里（FixedUpdate 阶段），**全工程只有那一处** —— 编辑器的 Play /
// Simulate 和独立 player 因此跑的是同一份，不会出现「编辑器里能掉、exe 里不动」。
//
// b3* 的类型一个都不出现在这个头里：这个头会被 World 那条链上的东西包含，而 World 的头编辑器和
// player 都包含 —— box3d 的头不该跟着扩散出去。所以全部状态在 Impl 里。
class PhysicsSystem final : public scene::SceneSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem() override;

    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    void onUpdate(scene::Scene& scene, const scene::UpdateContext& context) override;

    // 从 origin 沿 translation 打一枪，返回最近命中。没有物理世界、或什么都没打中，返回 nullopt。
    //
    // translation 是位移向量，不是「方向 × 长度」的另一种写法 —— 和 Box3D 的
    // b3World_CastRayClosest 一致。Lua 绑定那边的文档也按这个写。
    //
    // 命中的实体 UUID 来自建 body 时塞进 userData 的那份，所以没进模拟的实体（被跳过的、
    // 只有 collider 没有 body 的）打不中。
    struct RaycastHit {
        core::UUID entity;
        glm::vec3 point{ 0.0f };
        glm::vec3 normal{ 0.0f };
        float fraction{ 1.0f };
    };
    std::optional<RaycastHit> raycast(const glm::vec3& origin, const glm::vec3& translation) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::engine
