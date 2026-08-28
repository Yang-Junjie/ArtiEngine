#pragma once
#include "artichoco/core/uuid.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::editor {

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
    std::optional<core::UUID> m_pending_delete;
    std::vector<core::UUID> m_visible_roots;
};

} // namespace arti::editor
