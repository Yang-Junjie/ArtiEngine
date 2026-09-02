#pragma once
#include "artichoco/scene/system.h"

#include <memory>

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

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::engine
