#include "editor_context.h"

#include "edit_history.h"
#include "editor_project.h"

#include "artichoco/core/application.h"
#include "artichoco/scene/scene.h"

namespace arti::editor {

struct EditorContext::State {
    // 模拟期间的场景快照。Stop 时拷回世界，所以 Simulate / Play 里的改动（物理写的
    // transform 也算）都不落在编辑的场景上。
    scene::Scene snapshot;
};

EditorContext::EditorContext(EditorProject& project)
        : m_project(&project),
          m_world(std::make_unique<engine::World>()),
          m_history(std::make_unique<EditHistory>()),
          m_state(std::make_unique<State>()) {
    // 拿一个基线，这样「刚起编辑器就按 Ctrl+Z」也是明确的空操作。真正的基线由
    // SceneDocument 在每次换场景之后重新给。
    m_history->reset(*m_world, std::nullopt);
}

EditorContext::~EditorContext() = default;

bool EditorContext::isProjectOpen() const noexcept {
    return m_project != nullptr && m_project->isOpen();
}

void EditorContext::enterMode(Mode mode) {
    if (mode == Mode::Edit) {
        exitToEdit();
        return;
    }
    if (m_mode == mode) {
        return;
    }
    // D8 不做 Simulate ↔ Play 的直接切换，但真被这么调时也不能把快照弄乱：先回 Edit
    // （把快照拷回去），再从干净的场景进另一个模式。这样「快照是谁的」永远只有一个答案。
    if (m_mode != Mode::Edit) {
        exitToEdit();
    }

    m_state->snapshot.copyEntitiesFrom(m_world->scene());
    m_mode = mode;
    // 一次新的模拟会话：固定步长余额和帧号从零开始 —— 物理系统正是靠帧号回退来重建世界的。
    m_world->resetClock();

    core::Application::get().getLogChannel().info("Entered {} mode (scene snapshotted)",
            mode == Mode::Play ? "Play" : "Simulate");
}

void EditorContext::exitToEdit() {
    if (m_mode == Mode::Edit) {
        return;
    }

    m_world->scene().copyEntitiesFrom(m_state->snapshot);
    m_mode = Mode::Edit;

    core::Application::get().getLogChannel().info("Returned to Edit mode (scene restored)");
}

void EditorContext::updateSimulation(float delta_time) { m_world->tick(delta_time); }

} // namespace arti::editor
