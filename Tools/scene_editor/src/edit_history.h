#pragma once
#include "artichoco/core/uuid.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace arti::engine {
class World;
} // namespace arti::engine

namespace arti::editor {

// 编辑器的撤销 / 重做栈。一条历史项 = 整个场景序列化出来的那段文本 + 当时选中的实体。
//
// **为什么是整场景快照而不是命令模式**：场景的写入点有 42 处以上，全都是把组件字段的地址交给
// ImGui 控件、控件当场改掉它（`inspector_panel.cpp` 那一片 `drawFloatRow(..., &light->intensity,
// ...)`），中间没有任何可以插钩子的层。命令模式的接入成本是「控件数 × 一个命令类」，而且每加
// 一个字段都要记得再加一个 —— 漏一个的症状是「这个字段撤不回来」，只有手动试到才会发现。
// 快照式的接入成本和字段数无关。代价是粒度只能到「整个场景」，以及每条历史项都是一份全量拷贝。
//
// **为什么存序列化文本而不是 `Scene` 克隆**（克隆更快更省，Play 模式的快照走的就是那条路）：
// 文本能直接比较，于是「这次交互到底改了什么没有」变成一行代码，而且是精确的。由此得到两个
// 连锁的好处：
//   1. 不可能产生空历史项 —— 点一下不改任何东西的控件、每帧无条件重写同一个值的钳制代码
//      （`inspector_panel.cpp:466`、`:549`），都不会污染历史栈。
//   2. **因此提交时机允许写得粗**：不用在每个控件上判断「它这一帧改了没有」，只要在「可能改了」
//      的时候查一次。这才是真正的收益 —— `inspector_panel.cpp` 一行都不用改。
// 「文本相同 ⇔ 场景相同」这一条成立，靠的是 `SceneSerializer` 把实体按 UUID、组件按类型名都排过
// 序（`scene_serializer.cpp:36-48`）。`scene_snapshot_smoke` 里的 `canonicalFormHolds()` 钉着它。
//
// **这个类不认识 `EditorContext`，也不知道编辑器的模式。** World 和选中都按参数传进来，
// 「模拟中不记历史」那条判断留在 `EditorLayer`。让它自己去问模式，就等于让「历史栈为什么没记
// 这一条」变成要同时读两个类才能回答的问题。
//
// 完整的设计记录见 docs/tasks/2026-09-04-editor-undo-redo.md。
class EditHistory {
public:
    // 撤销栈的**双上限**：条数和字节数，任一超了就从最旧的一端丢。
    //
    // 只限条数会在大场景上炸内存（几千个实体的 dump 是 MB 级，64 条就是几百 MB）；只限字节会让
    // 小场景的历史长得没必要（一个默认场景能存几千条，而没人连按一千次 Ctrl+Z）。两条一起才是
    // 「小场景够用、大场景不炸」。重做栈不设独立上限 —— 它的长度天然被撤销次数夹住。
    static constexpr std::size_t kMaxEntries = 64;
    static constexpr std::size_t kMaxBytes = 64u * 1024u * 1024u;

    // 换场景（新建 / 打开 / 读失败）之后重新拿基线，两条栈都清掉。
    //
    // **调用顺序有讲究**：要在场景已经摆好之后调。`SceneDocument::reset()` 里调一次是为了让读
    // 失败留下的空场景也有个一致的基线，但 `createNew()` 必须在 `populateDefault()` **之后**再
    // 调一次 —— 否则基线是那个空场景，默认场景就成了「一次未提交的改动」，第一次 Ctrl+Z 会把它
    // 整个抹掉。
    void reset(const engine::World& world, const std::optional<core::UUID>& selection);

    // 每帧最开头调一次，记下这一帧**开始时**的选中。
    //
    // 为什么是帧初而不是帧末：删除是在 `HierarchyPanel::draw()` 开头落地的，落地时会把选中清掉。
    // 帧末取的话，「删掉 E」这条历史项记下来的选中是空，撤销回去 E 回来了却没被选中；帧初取的
    // 是 E，撤销回去直接选上。后者才是想要的。
    void beginFrame(const std::optional<core::UUID>& selection);

    // 「我刚改了场景，帧末查一下」。只给**不经过 ImGui 控件**的改动用：键盘快捷键、延迟到下一帧
    // 执行的复制 / 删除、右键菜单里的建实体、拖资产进 Viewport。经过控件的改动靠
    // `commitIfChanged` 的 interaction_ended 覆盖，不需要在这儿报到。
    void requestCommit();

    // 帧末调。`interaction_ended` = 这一帧是不是刚有一次交互结束（ImGui 的 ActiveId 或 gizmo 的
    // 下降沿）。它和 requestCommit() 任一成立就查一次场景，**真的变了才压一条历史项**。
    //
    // 返回是否压了。调用方在模拟中不该调这个（D5）—— beginFrame 会把攒下的请求清掉，
    // 所以模拟期间的改动不会漏到 Stop 之后才被记上。
    bool commitIfChanged(const engine::World& world, const std::optional<core::UUID>& selection,
            bool interaction_ended);

    // 栈里有没有东西。「能不能撤销」还要看模式和有没有正在进行的交互，那两条在 `EditorLayer`。
    bool canUndo() const noexcept { return !m_undo.empty(); }
    bool canRedo() const noexcept { return !m_redo.empty(); }

    // 恢复上 / 下一个状态，把选中一起写回 `selection`。
    //
    // 恢复失败（那条历史项坏了）时返回 false，**场景一点不变**（`World::restoreScene()` 保证），
    // 并且把坏掉的那一条**丢掉**。不丢的话每按一次 Ctrl+Z 都会在同一条上撞一次，用户永远走不过
    // 那个点；丢掉之后下一次撤销落到它前面那条，至少是往前走的。
    bool undo(engine::World& world, std::optional<core::UUID>& selection);
    bool redo(engine::World& world, std::optional<core::UUID>& selection);

    // 当前状态的编号，每次提交换一个新的。`SceneDocument` 拿它和「存盘时是哪个」比，
    // 得到一个精确的脏标记 —— 而且**撤销回存盘时的那个状态会自动变回干净**，
    // 这是编号方案唯一比「提交就置脏」强的地方，也是它值得的地方。
    std::uint64_t currentStateId() const noexcept { return m_current.state_id; }

private:
    struct Entry {
        std::string text;
        std::optional<core::UUID> selection;
        std::uint64_t state_id{ 0 };
    };

    bool step(std::deque<Entry>& from, std::deque<Entry>& to, engine::World& world,
            std::optional<core::UUID>& selection);
    void trim();
    std::size_t totalBytes() const;

    std::deque<Entry> m_undo;
    std::deque<Entry> m_redo;
    Entry m_current;

    std::optional<core::UUID> m_frame_selection;
    bool m_commit_requested{ false };
    std::uint64_t m_next_state_id{ 1 };
};

} // namespace arti::editor
