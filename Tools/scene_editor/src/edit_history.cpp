#include "edit_history.h"

#include "runtime/world.h"

#include "artichoco/scene/scene.h"

#include <utility>

namespace arti::editor {

void EditHistory::reset(const engine::World& world, const std::optional<core::UUID>& selection) {
    m_undo.clear();
    m_redo.clear();
    m_commit_requested = false;
    m_frame_selection = selection;
    m_current = Entry{ world.captureScene(), selection, m_next_state_id++ };
}

void EditHistory::beginFrame(const std::optional<core::UUID>& selection) {
    m_frame_selection = selection;
    // 攒下的请求每帧清一次。模拟中调用方不查提交（D5），靠这一下把那期间的请求丢掉 ——
    // 否则 Stop 之后第一个下降沿会把「模拟期间点过一下」当成一次编辑记上。
    m_commit_requested = false;
}

void EditHistory::requestCommit() { m_commit_requested = true; }

bool EditHistory::commitIfChanged(const engine::World& world,
        const std::optional<core::UUID>& selection, bool interaction_ended) {
    if (!m_commit_requested && !interaction_ended) {
        return false;
    }
    m_commit_requested = false;

    std::string text = world.captureScene();
    // 空串意味着序列化本身失败了（已经记过 error）。当成「没变」处理：把一段空文本推进历史，
    // 下次撤销到它就是把场景清空 —— 那比丢掉这一条历史项糟得多。
    if (text.empty() || text == m_current.text) {
        return false;
    }

    // 压进 undo 栈的是**旧文本 + 帧初的选中**（见头文件里 beginFrame 的说明）。
    m_undo.push_back(Entry{ std::move(m_current.text), m_frame_selection, m_current.state_id });
    m_current = Entry{ std::move(text), selection, m_next_state_id++ };
    // 新的分支落地，原来那条重做链就不再可达了。
    m_redo.clear();
    trim();
    return true;
}

bool EditHistory::undo(engine::World& world, std::optional<core::UUID>& selection) {
    return step(m_undo, m_redo, world, selection);
}

bool EditHistory::redo(engine::World& world, std::optional<core::UUID>& selection) {
    return step(m_redo, m_undo, world, selection);
}

bool EditHistory::step(std::deque<Entry>& from, std::deque<Entry>& to, engine::World& world,
        std::optional<core::UUID>& selection) {
    if (from.empty()) {
        return false;
    }

    // 先恢复，成功了才动两条栈。restoreScene 失败时场景一点没变，所以「历史栈和场景仍然一致」
    // 这条不变式保住了。把坏掉的那条丢掉：不丢的话每按一次 Ctrl+Z 都在同一条上撞一次，用户
    // 永远走不过那个点。失败的原因 restoreScene 自己记过 error，这里不重复报。
    if (!world.restoreScene(from.back().text)) {
        from.pop_back();
        return false;
    }

    // 被替换掉的那个状态进对面那条栈，选中取**现在**的 —— 走回来时看到的就是刚离开的样子。
    to.push_back(Entry{ std::move(m_current.text), selection, m_current.state_id });
    m_current = std::move(from.back());
    from.pop_back();

    selection = m_current.selection;
    // 防一手：历史项里的选中理论上一定存在于同一条历史项的文本里，但真对不上时宁可没有选中，
    // 也不要留一个查不到实体的 UUID 让面板每帧去空查。
    if (selection && !world.scene().containsEntity(*selection)) {
        selection.reset();
    }

    trim();
    return true;
}

void EditHistory::trim() {
    // 两条上限都从**最旧**的一端丢。重做栈不设独立上限：它的长度天然被撤销次数夹住。
    while (!m_undo.empty() && (m_undo.size() > kMaxEntries || totalBytes() > kMaxBytes)) {
        m_undo.pop_front();
    }
}

std::size_t EditHistory::totalBytes() const {
    // 每次 trim 重算而不是增量维护：条数上限 64，重算是白给的，而增量维护是一类容易写错、
    // 错了又不报错的 bug（预算算漂了只会表现成历史深度莫名其妙）。
    std::size_t bytes = m_current.text.size();
    for (const auto& entry: m_undo) {
        bytes += entry.text.size();
    }
    for (const auto& entry: m_redo) {
        bytes += entry.text.size();
    }
    return bytes;
}

} // namespace arti::editor
