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

    // 请求复制一个实体（连同它整棵子树）。真正的复制推迟到下一次 draw() 的开头 —— 在遍历
    // 实体、画着 ImGui 树的中途改 registry 是自找的麻烦。菜单栏和 Ctrl+D 都走这里。
    void requestDuplicate(core::UUID entity);

    // 请求删除一个实体（连同它整棵子树 —— `Scene::destroyEntity()` 的语义）。同样是延迟执行。
    void requestDelete(core::UUID entity);

private:
    void drawEntityNode(core::UUID entity);

    EditorContext& m_context;
    std::optional<core::UUID> m_pending_delete;
    std::optional<core::UUID> m_pending_duplicate;
    std::vector<core::UUID> m_visible_roots;
};

} // namespace arti::editor
