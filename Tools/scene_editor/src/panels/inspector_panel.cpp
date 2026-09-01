#include "panels/inspector_panel.h"

#include "editor_context.h"
#include "panels/ui_widgets.h"

#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"
#include "scene/component_registration.h"
#include "scene/component_serialization.h"
#include "scene/components.h"
#include "scene/render_scene_extractor.h"

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

// 极窄窗口下向量行内单根轴的拖动框最小宽度，防止宽度算出负数/挤成一团。
constexpr float kMinAxisWidth = 30.0f;

constexpr ImVec4 kAxisXColor{ 0.85f, 0.35f, 0.35f, 1.0f };
constexpr ImVec4 kAxisYColor{ 0.40f, 0.75f, 0.40f, 1.0f };
constexpr ImVec4 kAxisZColor{ 0.40f, 0.55f, 0.90f, 1.0f };

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

// Environment 只有一个 UUID 输入框，不值得为它再开一个 struct。
std::unordered_map<core::UUID, std::string>& environmentEditorStates() {
    static std::unordered_map<core::UUID, std::string> states;
    return states;
}

std::string uuidToText(core::UUID uuid) { return uuid.toString(); }

// ---- 属性网格里带 glm 的行（网格本身和通用行在 panels/ui_widgets.h）----

void drawColorRow(const char* label, glm::vec3& color, const char* tooltip = nullptr) {
    propertyRow(label, tooltip);
    // 和 drawFloatRow 同理：固定的 "##color" 在一个网格里出现两次就是同一个 ID。
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::ColorEdit3("##color", glm::value_ptr(color),
            ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB |
                    ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoTooltip);
    ImGui::PopID();
}

// Unity 风格向量控件：彩色 X/Y/Z 轴字母 + 拖动框，右键标签弹 Reset。
void drawVec3Control(const char* label, glm::vec3& value, const glm::vec3& reset_value, float speed,
        const char* format = "%.3f") {
    propertyRow(label);

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float chip_width = ImGui::CalcTextSize("X").x;

    const char* axes[] = { "X", "Y", "Z" };
    const ImVec4* axis_colors[] = { &kAxisXColor, &kAxisYColor, &kAxisZColor };
    float* components[] = { &value.x, &value.y, &value.z };

    // 行内 ID 必须带上 label，否则 Translation/Rotation/Scale 三行的轴控件 ID 全撞车，
    // 拖动状态和右键 Reset 会互相污染。
    ImGui::PushID(label);

    // 行内布局 = 3 根轴 + 2 个轴间距；每根轴内部 = 字母 + 间距 + 拖动框。
    // 注意实际间距有 5 个（轴内 3 个 + 轴间 2 个），只按 3 个算会把 Z 挤出右缘。
    const float avail = ImGui::GetContentRegionAvail().x;
    const float axis_total = (avail - 2.0f * spacing) / 3.0f;
    const float drag_width = std::max(axis_total - chip_width - spacing, kMinAxisWidth);

    // 以 Z 为基准从右往左锚定：Z 的右缘精确贴住列右缘，X/Y 依次向左排，
    // 不会因逐项累计的浮点误差把最后一根裁掉。
    const float row_y = ImGui::GetCursorScreenPos().y;
    const float row_right = ImGui::GetCursorScreenPos().x + avail;
    for (int i = 0; i < 3; ++i) {
        const float x = row_right - static_cast<float>(3 - i) * axis_total -
                        static_cast<float>(2 - i) * spacing;
        ImGui::SetCursorScreenPos(ImVec2{ x, row_y });
        ImGui::PushID(i);
        ImGui::TextColored(*axis_colors[i], "%s", axes[i]);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(drag_width);
        ImGui::DragFloat("##value", components[i], speed, 0.0f, 0.0f, format);
        ImGui::PopID();
    }
    ImGui::PopID();

    // 右键标签或拖动框 → Reset（id 绑定到标签，命中区域用最后一项的矩形）。
    if (ImGui::BeginPopupContextItem(label)) {
        if (ImGui::MenuItem("Reset")) {
            value = reset_value;
        }
        ImGui::EndPopup();
    }
}

// ---- 组件头部（通用） ----
// CollapsingHeader + 右键 Remove 菜单。
// 返回 false 表示节已关闭或组件已被删除（调用方应跳过正文）。
// Removable=false 时（如必需的 Transform）根本不实例化删除代码，编译期去掉。
template<typename Component, bool Removable = true>
bool drawComponentHeader(scene::Entity& entity, const char* label, bool default_open = true,
        const char* tooltip = nullptr) {
    const bool open = beginSection(label, default_open);
    const bool hovered = ImGui::IsItemHovered();

    if (hovered && tooltip != nullptr) {
        ImGui::SetTooltip("%s", tooltip);
    }

    bool remove_requested = false;
    if constexpr (Removable) {
        if (ImGui::BeginPopupContextItem(label)) {
            if (ImGui::MenuItem("Remove Component")) {
                remove_requested = true;
            }
            ImGui::EndPopup();
        }

        if (remove_requested) {
            entity.removeComponent<Component>();
            return false;
        }
    }
    return open;
}

// Add Component 弹窗里的菜单项：已存在的置灰并提示。
template<typename Component>
void drawAddMenuItem(const char* label, scene::Entity& entity) {
    const bool exists = entity.hasComponent<Component>();
    ImGui::BeginDisabled(exists);
    if (ImGui::MenuItem(label)) {
        entity.addComponent<Component>();
    }
    if (exists) {
        ImGui::SetItemTooltip("Already added to this entity");
    }
    ImGui::EndDisabled();
}

void drawAddComponentUI(scene::Entity& entity) {
    if (fullWidthDimButton("+  Add Component")) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    if (ImGui::BeginPopup("AddComponentPopup")) {
        ImGui::SeparatorText("Rendering");
        drawAddMenuItem<engine::MeshRendererComponent>("Mesh Renderer", entity);
        ImGui::SeparatorText("Lighting");
        drawAddMenuItem<engine::DirectionalLightComponent>("Directional Light", entity);
        drawAddMenuItem<engine::PointLightComponent>("Point Light", entity);
        drawAddMenuItem<engine::SpotLightComponent>("Spot Light", entity);
        ImGui::SeparatorText("Camera");
        drawAddMenuItem<engine::CameraComponent>("Camera", entity);
        ImGui::SeparatorText("Environment");
        drawAddMenuItem<engine::EnvironmentComponent>("Environment", entity);
        ImGui::EndPopup();
    }
}

} // namespace

InspectorPanel::InspectorPanel(EditorContext& context)
        : m_context(context) {}

void InspectorPanel::draw() {
    // 窗口被折叠或裁掉时 Begin 返回 false，此时 SkipItems 为真：BeginTable 会失败，
    // 而 propertyRow 里的 TableNextRow 会解引用空表指针。所以这里必须提前收工。
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }

    const auto& selected_entity = m_context.selectedEntity();
    if (!selected_entity) {
        drawEmptyState("No entity selected");
        ImGui::End();
        return;
    }

    auto entity = m_context.scene().findEntity(*selected_entity);
    if (!entity.isValid()) {
        drawEmptyState("Invalid entity");
        ImGui::End();
        return;
    }

    drawEntityInfo(entity);

    ImGui::Spacing();
    ImGui::Separator();
    drawAddComponentUI(entity);
    ImGui::Separator();

    drawTransformComponent(entity);
    drawCameraComponent(entity);
    drawMeshRendererComponent(entity);
    drawDirectionalLightComponent(entity);
    drawPointLightComponent(entity);
    drawSpotLightComponent(entity);
    drawEnvironmentComponent(entity);

    ImGui::End();
}

void InspectorPanel::drawEntityInfo(scene::Entity& entity) {
    auto* tag = tryGet<scene::TagComponent>(entity);
    if (tag == nullptr) {
        return;
    }

    char buffer[256]{};
    const auto length = std::min(tag->tag.size(), sizeof(buffer) - 1);
    std::memcpy(buffer, tag->tag.data(), length);

    // 实体名：加高内边距，视觉上更像一个「对象头」而不是普通属性行。
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 8.0f, 6.0f });
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputText("##entity_name", buffer, sizeof(buffer))) {
        tag->tag = buffer;
    }
    ImGui::PopStyleVar();

    // UUID：暗色整行可点，点击复制到剪贴板。
    const std::string uuid_text = uuidToText(entity.getUUID());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.55f, 0.55f, 0.58f, 1.0f });
    ImGui::Selectable(uuid_text.c_str(), false);
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked()) {
        ImGui::SetClipboardText(uuid_text.c_str());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Click to copy the entity UUID");
    }
}

void InspectorPanel::drawTransformComponent(scene::Entity& entity) {
    auto* transform = tryGet<scene::TransformComponent>(entity);
    if (transform == nullptr) {
        return;
    }

    // Transform 是必需的，不可删除，所以 Removable=false。
    if (!drawComponentHeader<scene::TransformComponent, false>(entity, "Transform")) {
        return;
    }

    beginPropertyGrid();
    drawVec3Control("Translation", transform->translation, glm::vec3{ 0.0f }, 0.1f);

    // 面板上显示角度（度），内部存四元数；只在值真的变了时才写回，避免每帧漂移。
    glm::vec3 euler_degrees = glm::degrees(glm::eulerAngles(transform->rotation));
    const glm::vec3 previous_euler = euler_degrees;
    drawVec3Control("Rotation", euler_degrees, glm::vec3{ 0.0f }, 0.5f, "%.1f");
    if (euler_degrees != previous_euler) {
        transform->rotation = glm::quat{ glm::radians(euler_degrees) };
    }

    drawVec3Control("Scale", transform->scale, glm::vec3{ 1.0f }, 0.01f);
    endPropertyGrid();
}

void InspectorPanel::drawCameraComponent(scene::Entity& entity) {
    auto* camera = tryGet<engine::CameraComponent>(entity);
    if (camera == nullptr) {
        return;
    }

    if (!drawComponentHeader<engine::CameraComponent>(entity, "Camera")) {
        return;
    }

    beginPropertyGrid();
    drawBoolRow("Primary", &camera->primary,
            "When multiple cameras exist, this one is used for rendering");
    drawFloatRow("FOV", &camera->fov_degrees, 0.1f, 1.0f, 179.0f, "%.1f deg");
    drawFloatRow("Near", &camera->near_plane, 0.01f, 0.001f, 10.0f);
    drawFloatRow("Far", &camera->far_plane, 1.0f, 1.0f, 10'000.0f);
    endPropertyGrid();
}

void InspectorPanel::drawMeshRendererComponent(scene::Entity& entity) {
    auto* mesh_renderer = tryGet<engine::MeshRendererComponent>(entity);
    if (mesh_renderer == nullptr) {
        return;
    }

    if (!drawComponentHeader<engine::MeshRendererComponent>(entity, "Mesh Renderer")) {
        return;
    }

    // UUID 输入框的文本要跨帧保留（用户可能输到一半），所以按实体存一份。
    auto& state = meshRendererEditorStates()[entity.getUUID()];
    if (mesh_renderer->mesh.id() != state.mesh_applied) {
        state.mesh_text = uuidToText(mesh_renderer->mesh.id());
        state.mesh_applied = mesh_renderer->mesh.id();
    }
    if (mesh_renderer->materials.size() != state.materials_applied.size()) {
        state.material_texts.clear();
        state.materials_applied.clear();
        for (const auto& material: mesh_renderer->materials) {
            state.material_texts.push_back(uuidToText(material.id()));
            state.materials_applied.push_back(material.id());
        }
    }

    beginPropertyGrid();

    propertyRow("Mesh");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    const bool mesh_valid = drawUuidInput("##mesh_uuid", state.mesh_text, state.mesh_applied);
    if (mesh_valid) {
        mesh_renderer->mesh =
                arti::asset::AssetHandle<engine::asset::MeshAsset>{ state.mesh_applied };
    }

    drawBoolRow("Visible", &mesh_renderer->visible);

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float remove_width =
            ImGui::CalcTextSize("Remove").x + ImGui::GetStyle().FramePadding.x * 2.0f + spacing;
    for (size_t i = 0; i < state.material_texts.size(); ++i) {
        propertyRow("Material");
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - remove_width);
        const bool material_valid = drawUuidInput("##material_uuid", state.material_texts[i],
                state.materials_applied[i]);
        if (material_valid && i < mesh_renderer->materials.size()) {
            mesh_renderer->materials[i] = arti::asset::AssetHandle<engine::asset::MaterialAsset>{
                state.materials_applied[i]
            };
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            state.material_texts.erase(
                    state.material_texts.begin() + static_cast<std::ptrdiff_t>(i));
            state.materials_applied.erase(
                    state.materials_applied.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < mesh_renderer->materials.size()) {
                mesh_renderer->materials.erase(
                        mesh_renderer->materials.begin() + static_cast<std::ptrdiff_t>(i));
            }
            --i;
        }
        ImGui::PopID();
    }

    endPropertyGrid();

    if (fullWidthDimButton("+  Add Material")) {
        state.material_texts.emplace_back(kUuidTextLength, '0');
        state.materials_applied.emplace_back(core::UUID{ 0 });
        mesh_renderer->materials.emplace_back();
    }
}

void InspectorPanel::drawDirectionalLightComponent(scene::Entity& entity) {
    auto* light = tryGet<engine::DirectionalLightComponent>(entity);
    if (light == nullptr) {
        return;
    }

    if (!drawComponentHeader<engine::DirectionalLightComponent>(entity, "Directional Light",
                /*default_open=*/true, "Direction comes from the Transform's -Z axis")) {
        return;
    }

    beginPropertyGrid();
    drawBoolRow("Enabled", &light->enabled);
    drawColorRow("Color", light->color);
    drawFloatRow("Intensity", &light->intensity, 0.1f, 0.0f, 100.0f, "%.2f");
    endPropertyGrid();
}

void InspectorPanel::drawPointLightComponent(scene::Entity& entity) {
    auto* light = tryGet<engine::PointLightComponent>(entity);
    if (light == nullptr) {
        return;
    }

    if (!drawComponentHeader<engine::PointLightComponent>(entity, "Point Light",
                /*default_open=*/true, "Position comes from the Transform")) {
        return;
    }

    beginPropertyGrid();
    drawBoolRow("Enabled", &light->enabled);
    drawColorRow("Color", light->color);
    drawFloatRow("Intensity", &light->intensity, 0.1f, 0.0f, 1000.0f, "%.2f");
    // 下限不是 0：range 为 0 时距离衰减整个塌掉，这个灯什么都照不亮，看起来像坏了。
    drawFloatRow("Range", &light->range, 0.1f, 0.01f, 1000.0f, "%.2f",
            "Distance at which the light fades to zero (1/d² falloff)");
    endPropertyGrid();
}

void InspectorPanel::drawSpotLightComponent(scene::Entity& entity) {
    auto* light = tryGet<engine::SpotLightComponent>(entity);
    if (light == nullptr) {
        return;
    }

    if (!drawComponentHeader<engine::SpotLightComponent>(entity, "Spot Light",
                /*default_open=*/true, "Position and -Z direction come from the Transform")) {
        return;
    }

    beginPropertyGrid();
    drawBoolRow("Enabled", &light->enabled);
    drawColorRow("Color", light->color);
    drawFloatRow("Intensity", &light->intensity, 0.1f, 0.0f, 1000.0f, "%.2f");
    drawFloatRow("Range", &light->range, 0.1f, 0.01f, 1000.0f, "%.2f");
    // 上限 89 而不是 90：到 90° 时锥面就是一个半平面，cos 为 0，角度衰减的分母会塌。
    drawFloatRow("Inner Cone", &light->inner_cone_degrees, 0.5f, 0.0f, 89.0f, "%.1f deg");
    drawFloatRow("Outer Cone", &light->outer_cone_degrees, 0.5f, 0.0f, 89.0f, "%.1f deg");
    endPropertyGrid();
    // 内锥比外锥大是无意义的配置（渲染端会夹住，但面板上先纠正过来，免得看着像 bug）。
    light->inner_cone_degrees = std::min(light->inner_cone_degrees, light->outer_cone_degrees);
}

void InspectorPanel::drawEnvironmentComponent(scene::Entity& entity) {
    auto* environment = tryGet<engine::EnvironmentComponent>(entity);
    if (environment == nullptr) {
        return;
    }

    if (!drawComponentHeader<engine::EnvironmentComponent>(entity, "Environment",
                /*default_open=*/true,
                "Without an equirect texture, the sky color acts as constant ambient")) {
        return;
    }

    auto& text = environmentEditorStates()[entity.getComponent<scene::IDComponent>().id];
    if (text.empty()) {
        text = uuidToText(environment->equirect_texture.id());
    }

    beginPropertyGrid();

    propertyRow("Equirect Texture");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    core::UUID applied{};
    if (drawUuidInput("##equirect_uuid", text, applied)) {
        environment->equirect_texture =
                arti::asset::AssetHandle<engine::asset::TextureAsset>{ applied };
    }

    drawBoolRow("Enabled", &environment->enabled);
    drawBoolRow("Sky Visible", &environment->sky_visible);
    drawColorRow("Sky Color", environment->sky_color);
    drawFloatRow("Intensity", &environment->intensity, 0.05f, 0.0f, 100.0f, "%.2f");

    endPropertyGrid();
}

} // namespace arti::editor
