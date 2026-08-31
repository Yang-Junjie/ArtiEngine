#include "editor_context.h"

#include "editor_project.h"

#include "artichoco/core/application.h"
#include "artichoco/core/timestep_accumulator.h"
#include "artichoco/scene/scene.h"
#include "artichoco/scene/system.h"

namespace arti::editor {

struct EditorContext::State {
    // Play 期间的场景快照。Stop 时拷回 m_scene，所以 Play 里的改动都不落在编辑的场景上。
    scene::Scene snapshot;
    core::FixedTimestepAccumulator fixed_accumulator;
    uint64_t play_frame_index{ 0 };
};

EditorContext::EditorContext(EditorProject& project)
        : m_project(&project),
          m_scene(std::make_unique<scene::Scene>()),
          m_state(std::make_unique<State>()) {}

EditorContext::~EditorContext() = default;

bool EditorContext::isProjectOpen() const noexcept {
    return m_project != nullptr && m_project->isOpen();
}

void EditorContext::enterPlayMode() {
    if (m_mode == Mode::Play) {
        return;
    }

    m_state->snapshot.copyEntitiesFrom(*m_scene);
    m_mode = Mode::Play;
    m_state->play_frame_index = 0;
    m_state->fixed_accumulator = core::FixedTimestepAccumulator{};

    core::Application::get().getLogChannel().info("Entered Play mode (scene snapshotted)");
}

void EditorContext::exitPlayMode() {
    if (m_mode == Mode::Edit) {
        return;
    }

    m_scene->copyEntitiesFrom(m_state->snapshot);
    m_mode = Mode::Edit;

    core::Application::get().getLogChannel().info("Returned to Edit mode (scene restored)");
}

void EditorContext::updatePlay(float delta_time) {
    scene::UpdateContext context;
    context.deltaTime = delta_time;
    context.fixedDeltaTime = m_state->fixed_accumulator.fixedDeltaTime();
    context.frameIndex = m_state->play_frame_index++;

    m_state->fixed_accumulator.tick(delta_time, [this, &context](float fixed_dt) {
        context.fixedDeltaTime = fixed_dt;
        m_scene->runSystems(scene::SystemStage::FixedUpdate, context);
    });

    m_scene->runSystems(scene::SystemStage::Update, context);
    m_scene->runSystems(scene::SystemStage::LateUpdate, context);
}

} // namespace arti::editor
