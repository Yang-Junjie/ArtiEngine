#include "panels/hierarchy_panel.h"

#include "edit_history.h"
#include "editor_context.h"

#include "artichoco/scene/components.h"
#include "artichoco/scene/scene.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace arti::editor {
namespace {

// 复制出来的实体改个不重名的名字。**这是编辑器的策略，不是场景的语义** ——
// `Scene::duplicateEntity()` 原样照抄名字，因为名字本来就不要求唯一（连点两次
// Create Empty Entity 也会得到两个 "Entity"）。但层级面板里两行一模一样根本分不出谁是谁，
// 所以这一层补上「Cube → Cube (1)」。
std::string uniqueTag(scene::Scene& scene, std::string_view tag) {
    std::string base{ tag };
    // 先剥掉已有的 " (n)" 后缀，否则复制三次会变成 "Cube (1) (1) (1)"。
    if (base.size() > 3 && base.back() == ')') {
        const auto open = base.rfind(" (");
        if (open != std::string::npos && open + 3 <= base.size() - 1) {
            const std::string digits = base.substr(open + 2, base.size() - open - 3);
            const bool all_digits = !digits.empty() &&
                    std::all_of(digits.begin(), digits.end(),
                            [](unsigned char c) { return std::isdigit(c) != 0; });
            if (all_digits) {
                base.erase(open);
            }
        }
    }

    for (int suffix = 1;; ++suffix) {
        std::string candidate = base + " (" + std::to_string(suffix) + ")";
        if (!scene.findEntityByTag(candidate).isValid()) {
            return candidate;
        }
    }
}

} // namespace

HierarchyPanel::HierarchyPanel(EditorContext& context)
        : m_context(context) {}

void HierarchyPanel::requestDuplicate(core::UUID entity) {
    m_pending_duplicate = entity;
}

void HierarchyPanel::requestDelete(core::UUID entity) {
    m_pending_delete = entity;
}

void HierarchyPanel::draw() {
    auto& scene = m_context.scene();

    // 上一帧攒下的改动在这里落地，**在 Begin() 之前** —— 面板被折叠时 Begin() 返回 false
    // 会直接 return，请求留在原处不会丢，但也别指望它当帧生效。Ctrl+D 和 Delete 是全局
    // 快捷键，折叠着 Hierarchy 按它们也必须有反应，所以这段不能放在那道 return 后面。
    if (m_pending_duplicate) {
        auto source = scene.findEntity(*m_pending_duplicate);
        if (source.isValid()) {
            auto copy = scene.duplicateEntity(source);
            copy.getComponent<scene::TagComponent>().tag =
                    uniqueTag(scene, source.getComponent<scene::TagComponent>().tag);
            m_context.setSelectedEntity(copy.getUUID());
            // 这一帧可能一个 ImGui item 都没动过（请求是上一帧下的，来源可能还是键盘），
            // 所以帧末那套下降沿信号覆盖不到这里 —— 显式报到一次。
            m_context.history().requestCommit();
        }
        m_pending_duplicate = std::nullopt;
    }

    if (m_pending_delete) {
        auto victim = scene.findEntity(*m_pending_delete);
        if (victim.isValid()) {
            scene.destroyEntity(victim);
            m_context.history().requestCommit();
        }
        // 按「选中的还在不在」清，而不是比对「删掉的是不是选中的那个」：destroyEntity()
        // 连整棵子树一起删，所以删一个祖先也会带走选中的那个实体。
        const auto& selected = m_context.selectedEntity();
        if (selected && !scene.findEntity(*selected).isValid()) {
            m_context.clearSelection();
        }
        m_pending_delete = std::nullopt;
    }

    // 窗口被折叠或裁掉时 Begin 返回 false，此时窗口的 SkipItems 为真，后面画什么都进不去，
    // 白白遍历一遍场景。
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
            // 菜单项本身是有下降沿的，但它在弹出层里 —— 多要一条请求比推理弹出层的 ID
            // 生命周期便宜。多报一次是无害的：EditHistory 比较文本，没变就不会压历史项。
            m_context.history().requestCommit();
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
            m_context.history().requestCommit();
        }
        if (ImGui::MenuItem("Duplicate Entity", "Ctrl+D")) {
            requestDuplicate(entity);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Entity", "Del")) {
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
        requestDelete(entity);
    }
}

} // namespace arti::editor
