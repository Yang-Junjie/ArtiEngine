// EditHistory 的行为测试。
//
// GUI 那半边的验收（拖一次 gizmo 只产生一条历史项、点了不改东西不产生历史项）只能人工做，
// 但**判断这件事的逻辑本身是纯的**：一个 World、一串「帧」信号、几段文本。所以那半边的规矩
// 在这里钉住，人工验收只需要确认「信号真的按预期到达」。
//
// 这个测试直接把 src/edit_history.cpp 编进来（编辑器是可执行不是库），所以它顺带守着一条：
// edit_history.cpp **不许依赖 Application 单例** —— 一旦有人在里面取 Application::get()，
// 这个测试会在链接期或运行期立刻炸，而不是等到某条错误路径在编辑器里被走到。

#include "edit_history.h"

#include "runtime/world.h"

#include "artichoco/core/log.h"
#include "artichoco/scene/components.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace arti;

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "edit_history_test: " << message << '\n';
    }
    return condition;
}

size_t entityCount(scene::Scene& target) {
    size_t count = 0;
    for (auto [handle, id]: target.view<scene::IDComponent>().each()) {
        (void)handle;
        (void)id;
        ++count;
    }
    return count;
}

// 编辑器一帧的形状：帧初记选中 → 面板改场景 → 帧末按信号查一次提交。
// `interaction_ended` 就是 EditorLayer 算出来的那个下降沿。
struct Frame {
    editor::EditHistory& history;
    engine::World& world;
    std::optional<core::UUID> selection;

    void begin() { history.beginFrame(selection); }

    bool end(bool interaction_ended) {
        return history.commitIfChanged(world, selection, interaction_ended);
    }
};

// 改一个能被序列化看见的字段。用名字而不是 transform：字符串比较不受浮点格式影响，
// 测试挂了就一定是历史栈的问题，不用先去排除「是不是精度」。
//
// **按 UUID 指名改，不要「改第一个实体」**：恢复一次快照之后实体的遍历顺序会变成 UUID 序
// （deserialize 按文件顺序建实体，而文件是按 UUID 排的），所以「第一个」在往返前后可能不是
// 同一个实体。第一版就是这么写的，症状是 redo 之后名字对不上，排查了半天才反应过来。
void renameEntity(scene::Scene& target, core::UUID id, const std::string& name) {
    target.findEntity(id).getComponent<scene::TagComponent>().tag = name;
}

std::string entityTag(scene::Scene& target, core::UUID id) {
    return target.findEntity(id).getComponent<scene::TagComponent>().tag;
}

int run() {
    engine::World world;
    auto& target = world.scene();
    const core::UUID kept = target.createEntity("Kept").getUUID();
    const core::UUID doomed = target.createEntity("Doomed").getUUID();

    editor::EditHistory history;
    history.reset(world, std::nullopt);
    Frame frame{ history, world, std::nullopt };

    // ---- 基线 ----
    if (!require(!history.canUndo() && !history.canRedo(), "刚 reset 完栈里居然有东西")) {
        return 1;
    }
    const std::uint64_t baseline_state = history.currentStateId();

    // ---- 有信号但场景没变 → 不产生历史项 ----
    // 这是 D2 的全部意义：提交时机允许写得粗，因为「没变」会被文本比较挡住。点一下复制 UUID
    // 那行、展开一个组件头、换个选中，走的都是这条路。
    frame.begin();
    history.requestCommit();
    if (!require(!frame.end(/*interaction_ended=*/true), "场景没变却压了一条历史项")) {
        return 1;
    }
    if (!require(!history.canUndo(), "空提交进了栈")) {
        return 1;
    }
    if (!require(history.currentStateId() == baseline_state, "空提交换了状态编号（脏标记会误亮）")) {
        return 1;
    }

    // ---- 改了但没有信号 → 也不提交 ----
    // 改动不会丢，它会攒到下一个信号。
    frame.begin();
    renameEntity(target, kept, "Renamed");
    if (!require(!frame.end(/*interaction_ended=*/false), "没有任何信号却提交了")) {
        return 1;
    }
    if (!require(!history.canUndo(), "没有信号却进了栈")) {
        return 1;
    }

    // ---- 一次拖拽合成一条历史项 ----
    // 模拟按住拖动框连改 5 帧、最后一帧松手（下降沿）。上面那次没提交的改动也一起被这一条收走。
    for (int step = 0; step < 5; ++step) {
        frame.begin();
        renameEntity(target, kept, step % 2 == 0 ? "Dragging A" : "Dragging B");
        if (!require(!frame.end(/*interaction_ended=*/false), "拖拽中途就提交了")) {
            return 1;
        }
    }
    frame.begin();
    renameEntity(target, kept, "Dragged");
    if (!require(frame.end(/*interaction_ended=*/true), "松手那一帧没有提交")) {
        return 1;
    }
    if (!require(history.canUndo() && !history.canRedo(), "提交之后 undo 栈应该有东西、redo 应该空")) {
        return 1;
    }
    if (!require(history.currentStateId() != baseline_state, "提交之后状态编号没变（脏标记不会亮）")) {
        return 1;
    }

    // ---- 撤销回到基线，重做再回来 ----
    std::optional<core::UUID> selection;
    if (!require(history.undo(world, selection), "undo 失败")) {
        return 1;
    }
    if (!require(entityTag(target, kept) == "Kept", "undo 之后名字没回到原来的值")) {
        return 1;
    }
    if (!require(history.currentStateId() == baseline_state,
                "undo 回基线之后状态编号不是基线那个 —— 脏标记不会变回干净")) {
        return 1;
    }
    if (!require(!history.canUndo() && history.canRedo(), "撤到底之后两条栈的状态不对")) {
        return 1;
    }
    if (!require(history.redo(world, selection), "redo 失败")) {
        return 1;
    }
    if (!require(entityTag(target, kept) == "Dragged", "redo 之后名字没回到撤销前的值")) {
        return 1;
    }

    // ---- 新的提交清空重做栈 ----
    if (!require(history.undo(world, selection), "undo 失败")) {
        return 1;
    }
    if (!require(history.canRedo(), "undo 之后 redo 栈应该有东西")) {
        return 1;
    }
    frame.begin();
    renameEntity(target, kept, "New Branch");
    if (!require(frame.end(true) && !history.canRedo(), "新的提交没有清掉重做栈")) {
        return 1;
    }

    // ---- 选中跟着历史项走（帧初的语义）----
    // 删掉当前选中的实体，撤销之后它必须回来**并且是选中的**。这条直接验「选中取帧初的值」：
    // 删除会在同一帧里把选中清掉，取帧末的话这里记下的就是空。
    frame.selection = doomed;
    frame.begin();
    target.destroyEntity(target.findEntity(doomed));
    frame.selection = std::nullopt;  // 面板在删除之后清掉了选中
    history.requestCommit();
    if (!require(frame.end(false), "删除之后的显式请求没有提交")) {
        return 1;
    }
    if (!require(!target.findEntity(doomed).isValid(), "测试自身有问题：实体没被删掉")) {
        return 1;
    }

    selection = std::nullopt;
    if (!require(history.undo(world, selection), "undo 失败")) {
        return 1;
    }
    if (!require(target.findEntity(doomed).isValid(), "undo 之后被删掉的实体没回来")) {
        return 1;
    }
    if (!require(selection.has_value() && *selection == doomed,
                "undo 之后被删掉的实体回来了但没被选中（选中取的是帧末而不是帧初？）")) {
        return 1;
    }

    // ---- 条数上限 ----
    // 提交远超上限的次数，然后一路撤到底：能撤的次数不该超过上限，而且撤到底之后**不是**
    // 最初那个场景（最旧的历史项已经被丢了）。
    editor::EditHistory capped;
    engine::World fresh;
    const core::UUID base = fresh.scene().createEntity("Base").getUUID();
    capped.reset(fresh, std::nullopt);
    Frame capped_frame{ capped, fresh, std::nullopt };

    constexpr size_t kCommits = editor::EditHistory::kMaxEntries + 20;
    for (size_t index = 0; index < kCommits; ++index) {
        capped_frame.begin();
        renameEntity(fresh.scene(), base, "Step " + std::to_string(index));
        if (!require(capped_frame.end(true), "第 " + std::to_string(index) + " 次提交没生效")) {
            return 1;
        }
    }
    size_t undo_steps = 0;
    while (capped.undo(fresh, selection)) {
        ++undo_steps;
        if (undo_steps > editor::EditHistory::kMaxEntries + 1) {
            break;
        }
    }
    if (!require(undo_steps == editor::EditHistory::kMaxEntries,
                "能撤销的步数（" + std::to_string(undo_steps) + "）和条数上限不一致")) {
        return 1;
    }
    if (!require(entityCount(fresh.scene()) == 1, "撤到底之后实体数不对")) {
        return 1;
    }
    // 撤到底停在的**不是**最初的 "Base"：最旧的 20 条已经被丢掉了。停在哪一条是可以算出来的
    // ——「第 kCommits - kMaxEntries 次提交之前」那个状态，也就是上一步改成的那个名字。
    // 这一条不只验上限，也验了「丢弃是从最旧的一端走」：从最新一端丢的话这里会是 "Base"。
    const std::string oldest_kept =
            "Step " + std::to_string(kCommits - editor::EditHistory::kMaxEntries - 1);
    if (!require(entityTag(fresh.scene(), base) == oldest_kept,
                "撤到底停在了 '" + entityTag(fresh.scene(), base) + "'，应该是 '" + oldest_kept +
                        "'（丢弃的方向反了？）")) {
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
