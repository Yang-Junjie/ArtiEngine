#pragma once
#include "runtime/world.h"

#include "artichoco/core/uuid.h"

#include <memory>
#include <optional>

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::editor {

class EditorProject;

class EditorContext {
public:
    enum class Mode { Edit, Play };

    explicit EditorContext(EditorProject& project);
    ~EditorContext();

    EditorContext(const EditorContext&) = delete;
    EditorContext& operator=(const EditorContext&) = delete;

    // 编辑器正在编辑的世界。Play 模式跑的就是它 —— 和独立 player 跑的是同一个 engine::World，
    // 所以「编辑器里 Play 的效果」和「exe 跑出来的效果」不会各自漂移。
    engine::World& world() noexcept { return *m_world; }
    const engine::World& world() const noexcept { return *m_world; }

    scene::Scene& scene() noexcept { return m_world->scene(); }
    const scene::Scene& scene() const noexcept { return m_world->scene(); }

    EditorProject& project() noexcept { return *m_project; }

    bool isProjectOpen() const noexcept;

    const std::optional<core::UUID>& selectedEntity() const noexcept { return m_selected_entity; }
    void setSelectedEntity(const std::optional<core::UUID>& entity) noexcept {
        m_selected_entity = entity;
    }
    void clearSelection() noexcept { m_selected_entity.reset(); }

    Mode mode() const noexcept { return m_mode; }
    bool isPlaying() const noexcept { return m_mode == Mode::Play; }

    // 进 Play：先把场景拷进快照，Stop 的时候原样拷回来。重复调用是空操作
    void enterPlayMode();
    // 回 Edit：从快照恢复场景。重复调用是空操作
    void exitPlayMode();

    // 跑一帧 Play 模式的系统。计时状态在 World 里 —— 那是运行时的一部分，不是编辑器的。
    void updatePlay(float delta_time);

private:
    struct State;

    EditorProject* m_project{ nullptr };
    std::unique_ptr<engine::World> m_world;
    std::unique_ptr<State> m_state;
    std::optional<core::UUID> m_selected_entity;
    Mode m_mode{ Mode::Edit };
};

} // namespace arti::editor
