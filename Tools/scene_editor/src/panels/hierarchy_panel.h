#pragma once
#include "artichoco/core/uuid.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace arti::editor {

class EditorContext;

class HierarchyPanel {
public:
    explicit HierarchyPanel(EditorContext& context);

    void draw();

private:
    void drawEntityNode(core::UUID entity);

    EditorContext& m_context;
    std::optional<core::UUID> m_pending_delete;
    std::vector<core::UUID> m_visible_roots;
};

} // namespace arti::editor
