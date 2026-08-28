#pragma once
#include "artichoco/core/uuid.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::editor {

// Hierarchy 面板：显示场景树，支持选中、创建、删除实体。
class HierarchyPanel {
public:
    explicit HierarchyPanel(scene::Scene& scene);

    void draw();

    std::optional<core::UUID> selectedEntity() const noexcept { return m_selected_entity; }
    void setSelectedEntity(const std::optional<core::UUID>& entity) noexcept {
        m_selected_entity = entity;
    }

private:
    void drawEntityNode(core::UUID entity);

    scene::Scene& m_scene;
    std::optional<core::UUID> m_selected_entity;
    // 删除延迟到树画完再做：遍历中销毁实体会让 entt 的 view 失效。
    std::optional<core::UUID> m_pending_delete;
    // 每帧重建的顶层实体列表，成员而不是局部变量是为了留住容量。
    std::vector<core::UUID> m_visible_roots;
};

} // namespace arti::editor
