#include "panels/hierarchy_panel.h"

#include "editor_context.h"

#include "artichoco/scene/components.h"
#include "artichoco/scene/scene.h"

#include <imgui.h>

namespace arti::editor {

HierarchyPanel::HierarchyPanel(EditorContext& context)
        : m_context(context) {}

void HierarchyPanel::draw() {
    auto& scene = m_context.scene();

    // 窗口被折叠或裁掉时 Begin 返回 false，此时窗口的 SkipItems 为真，后面画什么都进不去，
    // 白白遍历一遍场景。删除请求下一次可见时再处理，不会丢。
    if (!ImGui::Begin("Hierarchy")) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginPopupContextWindow(nullptr,
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Empty Entity")) {
            auto entity = scene.createEntity("Entity");
            const auto& id = entity.getComponent<scene::IDComponent>();
            m_context.setSelectedEntity(id.id);
        }
        ImGui::EndPopup();
    }

    m_visible_roots.clear();
    for (auto [entity_handle, id]: scene.view<scene::IDComponent>().each()) {
        auto entity = scene.findEntity(id.id);
        if (!entity.getComponent<scene::ParentComponent>().parent_id.isValid()) {
            m_visible_roots.push_back(id.id);
        }
    }
    for (const auto root: m_visible_roots) {
        drawEntityNode(root);
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() &&
            !ImGui::IsAnyItemHovered()) {
        m_context.clearSelection();
    }

    ImGui::End();

    if (m_pending_delete) {
        auto victim = scene.findEntity(*m_pending_delete);
        if (victim.isValid()) {
            scene.destroyEntity(victim);
        }
        const auto& selected = m_context.selectedEntity();
        if (selected && *selected == *m_pending_delete) {
            m_context.clearSelection();
        }
        m_pending_delete = std::nullopt;
    }
}

void HierarchyPanel::drawEntityNode(core::UUID entity) {
    auto& scene = m_context.scene();

    auto entity_handle = scene.findEntity(entity);
    if (!entity_handle.isValid()) {
        return;
    }

    const auto& tag = entity_handle.getComponent<scene::TagComponent>();

    const auto children = scene.getChildren(entity_handle);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    const auto& selected = m_context.selectedEntity();
    if (selected && *selected == entity) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }

    const bool opened =
            ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(entity.value())),
                    flags, "%s", tag.tag.c_str());

    if (ImGui::IsItemClicked()) {
        m_context.setSelectedEntity(entity);
    }

    bool delete_requested = false;
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Create Child Entity")) {
            auto child = scene.createEntity("Entity");
            scene.setParent(child, entity_handle);
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
