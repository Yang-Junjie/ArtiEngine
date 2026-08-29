#include "inspector_panel.h"

#include "arti_engine.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace arti::editor {
namespace {

template<typename Component>
Component* tryGet(scene::Entity& entity) {
    return entity.hasComponent<Component>() ? &entity.getComponent<Component>() : nullptr;
}

constexpr std::size_t kUuidTextLength = 16;

struct MeshRendererEditorState {
    std::string mesh_text;
    core::UUID mesh_applied{};

    std::vector<std::string> material_texts;
    std::vector<core::UUID> materials_applied{};
};

std::unordered_map<core::UUID, MeshRendererEditorState>& meshRendererEditorStates() {
    static std::unordered_map<core::UUID, MeshRendererEditorState> states;
    return states;
}

std::string uuidToText(core::UUID uuid) {
    return uuid.toString();
}

bool parseUuidText(const std::string& text, core::UUID& out) {
    if (const auto parsed = core::UUID::fromString(text)) {
        out = *parsed;
        return true;
    }
    return false;
}

bool drawUuidInput(const char* label, std::string& text, core::UUID& applied) {
    char buffer[kUuidTextLength + 1]{};
    std::memcpy(buffer, text.data(), std::min(text.size(), kUuidTextLength));

    if (ImGui::InputText(label, buffer, sizeof(buffer),
                ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_AutoSelectAll)) {
        text.assign(buffer);
    }
    return parseUuidText(text, applied);
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

    if (ImGui::SmallButton("Remove##Camera")) {
        entity.removeComponent<engine::CameraComponent>();
    }
}

void InspectorPanel::drawMeshRendererComponent(scene::Entity& entity) {
    if (!ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    auto* mesh_renderer = tryGet<engine::MeshRendererComponent>(entity);
    auto& state = meshRendererEditorStates()[entity.getUUID()];

    if (mesh_renderer != nullptr) {
        if (mesh_renderer->mesh.id() != state.mesh_applied) {
            state.mesh_text = uuidToText(mesh_renderer->mesh.id());
            state.mesh_applied = mesh_renderer->mesh.id();
        }
        if (mesh_renderer->materials.size() != state.materials_applied.size()) {
            state.material_texts.clear();
            state.materials_applied.clear();
            for (const auto& material : mesh_renderer->materials) {
                state.material_texts.push_back(uuidToText(material.id()));
                state.materials_applied.push_back(material.id());
            }
        }
    }

    const bool mesh_valid = drawUuidInput("Mesh UUID", state.mesh_text, state.mesh_applied);
    if (mesh_valid && mesh_renderer != nullptr) {
        mesh_renderer->mesh =
                arti::asset::AssetHandle<engine::asset::MeshAsset>{ state.mesh_applied };
    }

    for (size_t i = 0; i < state.material_texts.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool material_valid = drawUuidInput("Material", state.material_texts[i],
                state.materials_applied[i]);
        if (material_valid && mesh_renderer != nullptr && i < mesh_renderer->materials.size()) {
            mesh_renderer->materials[i] =
                    arti::asset::AssetHandle<engine::asset::MaterialAsset>{ state.materials_applied[i] };
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            state.material_texts.erase(state.material_texts.begin() + static_cast<std::ptrdiff_t>(i));
            state.materials_applied.erase(
                    state.materials_applied.begin() + static_cast<std::ptrdiff_t>(i));
            if (mesh_renderer != nullptr && i < mesh_renderer->materials.size()) {
                mesh_renderer->materials.erase(
                        mesh_renderer->materials.begin() + static_cast<std::ptrdiff_t>(i));
            }
            --i;
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("Add Material")) {
        state.material_texts.emplace_back(kUuidTextLength, '0');
        state.materials_applied.emplace_back(core::UUID{ 0 });
        if (mesh_renderer != nullptr) {
            mesh_renderer->materials.emplace_back();
        }
    }

    if (mesh_renderer == nullptr) {
        if (ImGui::SmallButton("Add Component##MeshRenderer")) {
            engine::MeshRendererComponent component;
            if (mesh_valid) {
                component.mesh =
                        arti::asset::AssetHandle<engine::asset::MeshAsset>{ state.mesh_applied };
            }
            for (size_t i = 0; i < state.material_texts.size(); ++i) {
                core::UUID material_uuid;
                if (parseUuidText(state.material_texts[i], material_uuid)) {
                    component.materials.push_back(
                            arti::asset::AssetHandle<engine::asset::MaterialAsset>{ material_uuid });
                }
            }
            entity.addComponent<engine::MeshRendererComponent>(std::move(component));
        }
    } else {
        ImGui::Checkbox("Visible", &mesh_renderer->visible);
        if (ImGui::SmallButton("Remove##MeshRenderer")) {
            entity.removeComponent<engine::MeshRendererComponent>();
        }
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
    ImGui::TextDisabled("Direction comes from Transform's -Z axis");

    if (ImGui::SmallButton("Remove##DirectionalLight")) {
        entity.removeComponent<engine::DirectionalLightComponent>();
    }
}

} // namespace arti::editor
