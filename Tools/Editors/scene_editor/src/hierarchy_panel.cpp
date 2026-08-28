#include "hierarchy_panel.h"

#include "artichoco/scene/components.h"
#include "artichoco/scene/scene.h"

#include <imgui.h>

namespace arti::editor {

HierarchyPanel::HierarchyPanel(scene::Scene& scene)
        : m_scene(scene) {}

void HierarchyPanel::draw() {
    ImGui::Begin("Hierarchy");

    // 右键空白区域创建实体
    if (ImGui::BeginPopupContextWindow(nullptr,
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Empty Entity")) {
            auto entity = m_scene.createEntity("Entity");
            const auto& id = entity.getComponent<scene::IDComponent>();
            m_selected_entity = id.id;
        }
        ImGui::EndPopup();
    }

    // 只画顶层实体（没有父节点的），子节点由 drawEntityNode 递归。
    //
    // 先收集再画：ImGui 的右键菜单里可能删实体，那会让 entt 的 view 在遍历中失效。
    m_visible_roots.clear();
    for (auto [entity_handle, id]: m_scene.view<scene::IDComponent>().each()) {
        auto entity = m_scene.findEntity(id.id);
        // ParentComponent 是必备组件（每个实体都有），所以直接取；parent_id 无效就是顶层。
        if (!entity.getComponent<scene::ParentComponent>().parent_id.isValid()) {
            m_visible_roots.push_back(id.id);
        }
    }
    for (const auto root: m_visible_roots) {
        drawEntityNode(root);
    }

    // 点击空白区域取消选中
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() &&
            !ImGui::IsAnyItemHovered()) {
        m_selected_entity = std::nullopt;
    }

    ImGui::End();

    // 树画完了才真的删 —— 在遍历中间销毁实体会让 entt 的 view 失效。
    if (m_pending_delete) {
        auto victim = m_scene.findEntity(*m_pending_delete);
        if (victim.isValid()) {
            m_scene.destroyEntity(victim);
        }
        if (m_selected_entity && *m_selected_entity == *m_pending_delete) {
            m_selected_entity = std::nullopt;
        }
        m_pending_delete = std::nullopt;
    }
}

void HierarchyPanel::drawEntityNode(core::UUID entity) {
    auto entity_handle = m_scene.findEntity(entity);
    if (!entity_handle.isValid()) {
        return;
    }

    const auto& tag = entity_handle.getComponent<scene::TagComponent>();

    const auto children = m_scene.getChildren(entity_handle);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (m_selected_entity && *m_selected_entity == entity) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    const bool opened =
            ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(entity.value())),
                    flags, "%s", tag.tag.c_str());

    if (ImGui::IsItemClicked()) {
        m_selected_entity = entity;
    }

    // 删除要等这一帧的树画完再执行 —— 在遍历里销毁实体会让 entt 的存储失效。
    bool delete_requested = false;
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Create Child Entity")) {
            auto child = m_scene.createEntity("Entity");
            m_scene.setParent(child, entity_handle);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Entity")) {
            delete_requested = true;
        }
        ImGui::EndPopup();
    }

    if (opened) {
        for (const auto& child: children) {
            drawEntityNode(child.getComponent<scene::IDComponent>().id);
        }
        ImGui::TreePop();
    }

    if (delete_requested) {
        m_pending_delete = entity;
    }
}

} // namespace arti::editor
