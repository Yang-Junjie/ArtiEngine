#pragma once
#include "runtime/world.h"

#include "artichoco/core/uuid.h"

#include <memory>
#include <optional>

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::editor {

class EditHistory;
class EditorProject;

class EditorContext {
public:
    // 三种模式的区别只有两条**互相独立**的轴：跑不跑系统、是不是游戏视角。
    //
    //             跑系统   相机           gizmo / 调试线
    //   Edit        否    编辑器相机           画
    //   Simulate    是    编辑器相机           画
    //   Play        是    场景的 primary       不画
    //
    // 也就是说 Simulate = 系统在跑，但你还坐在编辑器里。只允许 Edit ↔ Simulate 和
    // Edit ↔ Play，不做 Simulate ↔ Play 的直接切换（那要先回答「切过去之后快照算谁的」）。
    enum class Mode { Edit, Simulate, Play };

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

    // 撤销 / 重做栈。放在这里而不是 EditorLayer 里，是因为面板也要报到（右键菜单建实体、
    // 延迟落地的复制 / 删除），而面板手里本来就有 EditorContext —— 不用再拉一条 plumbing。
    EditHistory& history() noexcept { return *m_history; }
    const EditHistory& history() const noexcept { return *m_history; }

    bool isProjectOpen() const noexcept;

    const std::optional<core::UUID>& selectedEntity() const noexcept { return m_selected_entity; }
    void setSelectedEntity(const std::optional<core::UUID>& entity) noexcept {
        m_selected_entity = entity;
    }
    void clearSelection() noexcept { m_selected_entity.reset(); }

    Mode mode() const noexcept { return m_mode; }

    // 上面那两条轴各自一个查询。**别加回一个 isPlaying()** —— 它恰好是这次要拆开的那个含混点，
    // 留着它下一个人就会拿它去判断「要不要画 gizmo」，于是 Simulate 下 gizmo 就没了。
    bool isSimulating() const noexcept { return m_mode != Mode::Edit; }
    bool isGameView() const noexcept { return m_mode == Mode::Play; }

    // 进 Simulate 或 Play：先把场景拷进快照，回 Edit 时原样拷回来 —— 所以「模拟一下再撤销」
    // 是免费的。传 Mode::Edit 等于 exitToEdit()；重复进同一个模式是空操作。
    void enterMode(Mode mode);
    // 回 Edit：从快照恢复场景。已经在 Edit 就是空操作。
    void exitToEdit();

    // 跑一帧模拟（Simulate 和 Play 走的是同一条路）。计时状态在 World 里 —— 那是运行时的
    // 一部分，不是编辑器的。
    void updateSimulation(float delta_time);

private:
    struct State;

    EditorProject* m_project{ nullptr };
    std::unique_ptr<engine::World> m_world;
    std::unique_ptr<EditHistory> m_history;
    std::unique_ptr<State> m_state;
    std::optional<core::UUID> m_selected_entity;
    Mode m_mode{ Mode::Edit };
};

} // namespace arti::editor
