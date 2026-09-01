#pragma once
#include "artichoco/core/timestep_accumulator.h"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace arti::scene {
class Scene;
class SceneSerializationRegistry;
class SceneSerializer;
} // namespace arti::scene

namespace arti::engine {

// 一个正在跑的世界：场景本身、场景和磁盘之间那条路、以及推动它的时钟。
//
// 编辑器的 Play 模式和独立 player 驱动的是同一个 World。分成两份实现的话，「编辑器里 Play
// 出来的效果」和「exe 跑出来的效果」会各自漂移，而两边单独看都是对的 —— 这种 bug 最难找。
//
// 组件的拷贝表和序列化表在构造时注册（registerSceneComponents），所以 World 一建好就能
// 读写场景，调用方不需要记得先注册什么。
class World {
public:
    World();
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // 定义在 .cpp 里：这个头只前向声明 Scene，不想把 entt 拖给每个包含它的翻译单元。
    scene::Scene& scene() noexcept;
    const scene::Scene& scene() const noexcept;

    // 读一个场景，替换当前内容。失败时场景是空的 —— 不留半个读进来的场景假装成功。
    // 时钟一并归零：换了场景，上一个场景攒下的固定步长余额没有意义。
    bool loadScene(const std::filesystem::path& path);
    bool saveScene(const std::filesystem::path& path) const;

    // 清空实体并归零时钟。
    void clear();

    // 跑一帧：FixedUpdate 按固定步长补齐，然后 Update / LateUpdate 各一次。
    void tick(float delta_time);

    // 固定步长余额和帧号归零。进 Play 模式时调 —— 那是一次新会话的开始。
    void resetClock() noexcept;

    std::uint64_t frameIndex() const noexcept { return m_frame_index; }

private:
    std::unique_ptr<scene::Scene> m_scene;
    // 序列化表不是进程级的（是个对象），所以每个 World 自己持一份。
    std::unique_ptr<scene::SceneSerializationRegistry> m_serialization;
    std::unique_ptr<scene::SceneSerializer> m_serializer;

    core::FixedTimestepAccumulator m_fixed_accumulator;
    std::uint64_t m_frame_index{ 0 };
};

} // namespace arti::engine
