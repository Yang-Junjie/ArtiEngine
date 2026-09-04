#pragma once
#include "artichoco/core/timestep_accumulator.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

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

    // 场景 ↔ 内存里的一段文本。内容和 saveScene() 写进文件的完全一样，所以「存盘」和「快照」
    // 走的是同一条序列化路径，不会各自漂移。编辑器的撤销栈就是一叠这个（见
    // docs/tasks/2026-09-04-editor-undo-redo.md 的 D4）。
    //
    // 返回 std::string 而不是 YAML::Node：一是这个头不用拖 yaml-cpp，二是文本才是能比较、
    // 能哈希、能打出来看的形式 —— 撤销栈靠「文本相同 ⇔ 场景相同」来判断这一次交互到底改了
    // 什么没有，而那一条成立是因为 SceneSerializer 把实体按 UUID、组件按类型名都排过序。
    std::string captureScene() const;

    // 用 captureScene() 的文本替换当前场景。
    //
    // **失败时场景一点不变**，这和 loadScene() 刻意不同（那个失败时会清空）：读文件失败意味着
    // 「你要的场景不存在」，留半个更糟；而恢复一条历史项失败意味着历史栈坏了，这时候再把用户
    // 正在编辑的场景清掉纯属雪上加霜。这个性质是免费的 —— deserialize 先建 staging 场景并校验，
    // 抛异常都发生在替换之前。
    //
    // 也**不动时钟**：恢复历史项不是换场景，而且它只在编辑模式下用，时钟根本没在跑。归零反而
    // 会让物理在下一次进 Simulate 时多重建一次世界（帧号回退是它的重建信号）。
    bool restoreScene(std::string_view text);

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
