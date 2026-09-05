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

    // 让物理世界和场景对齐：新实体建 body、不再合格的实体拆掉 body、Static / Kinematic 的
    // transform 读进物理世界。
    //
    // **一个固定步里会调两次，两次都有各自的必要性**：
    //
    //   - 脚本的固定回调**之前**（`World::tick` 调）—— 会话的第一个固定步里脚本就能对刚体施力 /
    //     设速度。没有这一次，那一步的输入会掉在地上（body 还不存在）
    //   - `b3World_Step` **之前**（`onUpdate` 调）—— 脚本刚写进 TransformComponent 的运动学目标
    //     必须在这一个固定步里就生效，晚一步就是可见的延迟
    //
    // 两次之间没有 step，所以第二次算出来的和第一次一样（幂等），区别只是把脚本的改动带上了。
    // 因此把 PhysicsSystem 直接挂在别的地方（不经过 World）也不会漏同步，只是少一次。
    void syncBodies(scene::Scene& scene, float fixed_delta_time);

    // 新的一次模拟会话开始了：下一次同步之前把整个物理世界拆掉重建。
    //
    // `World::resetClock()` 调它。**光靠帧号回退当信号不够**：只跑了一帧就 Stop / Play 的话
    // 两次的 frameIndex 都是 0，`0 < 0` 不成立，旧世界会被当成还在用的（D5）。帧号回退仍然
    // 保留成兜底，给不经过 World 直接驱动 Scene 的调用方用。
    void requestSessionReset() noexcept;

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

    // 刚体控制（D3）。按实体 UUID 寻址 —— Box3D 的 body id 不出现在这个头里，脚本也不该
    // 拿着一个只有物理认识的句柄。
    //
    // 三条失败约定，都刻意是「返回失败」而不是抛异常或断言 —— 调用方是 Lua 脚本，一个手误
    // 不该带走进程：
    //   - 实体没有 body（不存在、缺组件、被跳过、还没进过模拟）→ nullopt / false
    //   - 速度 / 力 / 冲量用在非 Dynamic 上 → false。Kinematic 要动就写 TransformComponent，
    //     那是所有权规则（见 Scene.md 3.1.1），不是这里悄悄不生效
    //   - 非有限的输入 → false。它们会踩 Box3D 的 B3_ASSERT
    std::optional<glm::vec3> linearVelocity(core::UUID entity) const;
    bool setLinearVelocity(core::UUID entity, const glm::vec3& velocity);
    bool applyForce(core::UUID entity, const glm::vec3& force);
    bool applyImpulse(core::UUID entity, const glm::vec3& impulse);

    // 显式传送：物理位置和场景位置一起改，线速度和角速度清零。
    //
    // 需要 Scene 是因为它要改 TransformComponent：对 Static / Kinematic 来说场景才是权威，
    // 只挪 body 会在下一次同步时被场景的值拉回去。
    //
    // 「复位」和「高速运动」在物理看来完全不同 —— 用它把两者分开，别让一次复位被当成
    // 一帧走了十米的速度。
    bool teleport(scene::Scene& scene, core::UUID entity, const glm::vec3& position);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::engine
