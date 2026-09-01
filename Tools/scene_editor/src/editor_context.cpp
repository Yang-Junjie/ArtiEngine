#include "editor_context.h"

#include "editor_project.h"

#include "artichoco/core/application.h"
#include "artichoco/scene/scene.h"

namespace arti::editor {

struct EditorContext::State {
    // Play 期间的场景快照。Stop 时拷回世界，所以 Play 里的改动都不落在编辑的场景上。
    scene::Scene snapshot;
};

EditorContext::EditorContext(EditorProject& project)
        : m_project(&project),
          m_world(std::make_unique<engine::World>()),
          m_state(std::make_unique<State>()) {}

EditorContext::~EditorContext() = default;

bool EditorContext::isProjectOpen() const noexcept {
    return m_project != nullptr && m_project->isOpen();
}

void EditorContext::enterPlayMode() {
    if (m_mode == Mode::Play) {
        return;
    }

    m_state->snapshot.copyEntitiesFrom(m_world->scene());
    m_mode = Mode::Play;
    // 一次新的 Play 会话：固定步长余额和帧号从零开始。
    m_world->resetClock();

    core::Application::get().getLogChannel().info("Entered Play mode (scene snapshotted)");
}

void EditorContext::exitPlayMode() {
    if (m_mode == Mode::Edit) {
        return;
    }

    m_world->scene().copyEntitiesFrom(m_state->snapshot);
    m_mode = Mode::Edit;

    core::Application::get().getLogChannel().info("Returned to Edit mode (scene restored)");
}

void EditorContext::updatePlay(float delta_time) { m_world->tick(delta_time); }

} // namespace arti::editor
