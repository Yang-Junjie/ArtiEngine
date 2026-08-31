#pragma once
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

    scene::Scene& scene() noexcept { return *m_scene; }
    const scene::Scene& scene() const noexcept { return *m_scene; }

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

    // 跑一帧 Play 模式的系统（FixedUpdate 补齐后 Update / LateUpdate）。
    // 累加器和帧号都是这一次 Play 会话的状态，由 enterPlayMode 归零，所以留在这里。
    void updatePlay(float delta_time);

private:
    struct State;

    EditorProject* m_project{ nullptr };
    std::unique_ptr<scene::Scene> m_scene;
    std::unique_ptr<State> m_state;
    std::optional<core::UUID> m_selected_entity;
    Mode m_mode{ Mode::Edit };
};

} // namespace arti::editor
