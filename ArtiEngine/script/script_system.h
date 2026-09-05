#pragma once
#include "artichoco/scene/system.h"

#include <memory>

namespace arti::asset {
class AssetManager;
} // namespace arti::asset

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::engine {

// 按实体跑 Lua 脚本。挂在 World 构造函数的 Update 阶段，**全工程只有那一处** ——
// 编辑器的 Play / Simulate 和独立 player 因此跑同一份。
//
// Lua state 和每个实体的 environment 都在 pimpl 里：ScriptComponent 只有资产 handle，
// 快照 / 序列化碰不到 sol:: 的指针。
//
// 公开头一个 lua_ / sol:: 都不出现。
class ScriptSystem final : public scene::SceneSystem {
public:
    ScriptSystem();
    ~ScriptSystem() override;

    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    void setAssets(arti::asset::AssetManager* assets) noexcept;

    // 每渲染帧一次的 on_update(entity, dt)。
    void onUpdate(scene::Scene& scene, const scene::UpdateContext& context) override;

    // 每固定步一次的 on_fixed_update(entity, fixed_dt)。物理同步和解算**之前**派发，所以脚本
    // 在这里施的力 / 设的速度在同一个 b3World_Step 里就生效（D1）。
    //
    // 由 World::tick 显式调用，而不是注册成第二个 FixedUpdate 系统：那样会多出第二个
    // ScriptSystem 实例、第二份 sol::state，两套禁用状态各跑一半（一个手误的脚本会在固定
    // 时钟上被禁、在渲染时钟上继续跑）。一个实例服务两种回调，禁用因此是共享的。
    //
    // 一帧里可能是零次、一次或多次 —— 不要把「每帧一次」的语义搬进来。
    void onFixedUpdate(scene::Scene& scene, const scene::UpdateContext& context);

    // 新的一次模拟会话开始了：下一次派发之前整个 sol::state 拆掉重建。
    //
    // World::resetClock() 调它。**光靠帧号回退当信号不够**：只跑了一帧就 Stop / Play 的话
    // 两次的 frameIndex 都是 0，`0 < 0` 不成立，上一次会话的 VM 会被继承下来（D5）。
    // 帧号回退仍然保留成兜底。
    void requestSessionReset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::engine
