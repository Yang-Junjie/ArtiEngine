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
// 快照 / 序列化碰不到 sol:: 的指针。重建信号和物理一样，是 frameIndex 回退。
//
// 公开头一个 lua_ / sol:: 都不出现。
class ScriptSystem final : public scene::SceneSystem {
public:
    ScriptSystem();
    ~ScriptSystem() override;

    ScriptSystem(const ScriptSystem&) = delete;
    ScriptSystem& operator=(const ScriptSystem&) = delete;

    void setAssets(arti::asset::AssetManager* assets) noexcept;

    void onUpdate(scene::Scene& scene, const scene::UpdateContext& context) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace arti::engine
