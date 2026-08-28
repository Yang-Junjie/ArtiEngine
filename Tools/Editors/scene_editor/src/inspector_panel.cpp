#include "inspector_panel.h"

#include "arti_engine.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

namespace arti::editor {
namespace {

// Entity 只有 hasComponent + getComponent，没有 tryGetComponent，所以这里包一个。
// 返回指针让调用方能用「空就跳过」的写法。
template<typename Component>
Component* tryGet(scene::Entity& entity) {
    return entity.hasComponent<Component>() ? &entity.getComponent<Component>() : nullptr;
}

} // namespace

InspectorPanel::InspectorPanel(scene::Scene& scene)
        : m_scene(scene) {}

void InspectorPanel::draw(const std::optional<core::UUID>& selected_entity) {
    ImGui::Begin("Inspector");

    if (!selected_entity) {
        ImGui::TextDisabled("No entity selected");
        ImGui::End();
        return;
    }

    auto entity = m_scene.findEntity(*selected_entity);
    if (!entity.isValid()) {
        ImGui::TextDisabled("Invalid entity");
        ImGui::End();
        return;
    }

    drawTagComponent(entity);
    drawTransformComponent(entity);
    drawCameraComponent(entity);
    drawMeshRendererComponent(entity);
    drawDirectionalLightComponent(entity);

    ImGui::Separator();
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    if (ImGui::BeginPopup("AddComponentPopup")) {
        // 已经有的就置灰，addComponent 重复添加会抛。
        if (ImGui::MenuItem("Camera", nullptr, false,
                    !entity.hasComponent<engine::CameraComponent>())) {
            entity.addComponent<engine::CameraComponent>();
        }
        if (ImGui::MenuItem("Mesh Renderer", nullptr, false,
                    !entity.hasComponent<engine::MeshRendererComponent>())) {
            entity.addComponent<engine::MeshRendererComponent>();
        }
        if (ImGui::MenuItem("Directional Light", nullptr, false,
                    !entity.hasComponent<engine::DirectionalLightComponent>())) {
            entity.addComponent<engine::DirectionalLightComponent>();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

void InspectorPanel::drawTagComponent(scene::Entity& entity) {
    auto* tag = tryGet<scene::TagComponent>(entity);
    if (tag == nullptr) {
        return;
    }

    // ImGui 要可写的缓冲，所以拷进来编辑完再写回。
    char buffer[256]{};
    const auto length = std::min(tag->tag.size(), sizeof(buffer) - 1);
    std::memcpy(buffer, tag->tag.data(), length);

    if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
        tag->tag = buffer;
    }
}

void InspectorPanel::drawTransformComponent(scene::Entity& entity) {
    auto* transform = tryGet<scene::TransformComponent>(entity);
    if (transform == nullptr) {
        return;
    }

    if (!ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::DragFloat3("Translation", glm::value_ptr(transform->translation), 0.1f);

    // rotation 存的是四元数，UI 上给欧拉角（度）。
    //
    // 只在用户真的拖动时才写回：每帧做 quat -> euler -> quat 的往返会因为万向锁和
    // 多值表示导致数值漂移，看起来就是「没碰它，角度自己在变」。
    glm::vec3 euler_degrees = glm::degrees(glm::eulerAngles(transform->rotation));
    if (ImGui::DragFloat3("Rotation", glm::value_ptr(euler_degrees), 1.0f)) {
        transform->rotation = glm::quat{ glm::radians(euler_degrees) };
    }

    ImGui::DragFloat3("Scale", glm::value_ptr(transform->scale), 0.1f);
}

void InspectorPanel::drawCameraComponent(scene::Entity& entity) {
    auto* camera = tryGet<engine::CameraComponent>(entity);
    if (camera == nullptr) {
        return;
    }

    if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Checkbox("Primary", &camera->primary);
    ImGui::DragFloat("FOV", &camera->fov_degrees, 1.0f, 1.0f, 179.0f, "%.1f deg");
    ImGui::DragFloat("Near", &camera->near_plane, 0.01f, 0.001f, 10.0f);
    ImGui::DragFloat("Far", &camera->far_plane, 1.0f, 1.0f, 10'000.0f);
    // 没有 aspect：那是渲染目标的属性，由 extract 按目标尺寸算。

    if (ImGui::SmallButton("Remove##Camera")) {
        entity.removeComponent<engine::CameraComponent>();
    }
}

void InspectorPanel::drawMeshRendererComponent(scene::Entity& entity) {
    auto* mesh_renderer = tryGet<engine::MeshRendererComponent>(entity);
    if (mesh_renderer == nullptr) {
        return;
    }

    if (!ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Checkbox("Visible", &mesh_renderer->visible);
    // 现在只能看不能改：句柄要等资产层做起来才有得选。
    ImGui::Text("Mesh:     %s", mesh_renderer->mesh.isValid() ? "set" : "none");
    ImGui::Text("Material: %s", mesh_renderer->material.isValid() ? "set" : "none");

    int submesh = static_cast<int>(mesh_renderer->submesh_index);
    if (ImGui::DragInt("Submesh", &submesh, 1.0f, 0, 64)) {
        mesh_renderer->submesh_index = static_cast<uint32_t>(std::max(submesh, 0));
    }

    if (ImGui::SmallButton("Remove##MeshRenderer")) {
        entity.removeComponent<engine::MeshRendererComponent>();
    }
}

void InspectorPanel::drawDirectionalLightComponent(scene::Entity& entity) {
    auto* light = tryGet<engine::DirectionalLightComponent>(entity);
    if (light == nullptr) {
        return;
    }

    if (!ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    ImGui::Checkbox("Enabled", &light->enabled);
    ImGui::ColorEdit3("Color", glm::value_ptr(light->color));
    ImGui::DragFloat("Intensity", &light->intensity, 0.1f, 0.0f, 100.0f);
    // 没有 direction：方向取实体世界变换的 -Z 轴，用上面的 Rotation 调。
    ImGui::TextDisabled("Direction comes from Transform's -Z axis");

    if (ImGui::SmallButton("Remove##DirectionalLight")) {
        entity.removeComponent<engine::DirectionalLightComponent>();
    }
}

} // namespace arti::editor
