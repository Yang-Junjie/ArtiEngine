#include "render_scene_extractor.h"

#include "artichoco/scene/components.h"
#include "artichoco/scene/scene.h"
#include "asset/gpu_asset_cache.h"
#include "components.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace arti::engine {
namespace {

glm::vec3 forwardOf(const glm::mat4& world) noexcept {
    const glm::vec3 forward{ -world[2] };
    const float length = glm::length(forward);
    return length > 0.0f ? forward / length : glm::vec3{ 0.0f, -1.0f, 0.0f };
}

} // namespace

const rendering::RenderScene& RenderSceneExtractor::extract(scene::Scene& scene,
        asset::GpuAssetCache& assets, const rendering::Renderer& renderer, ExtractTarget target) {
    scene.updateWorldTransforms();

    m_render_scene.draws.clear();
    m_render_scene.lights.clear();
    m_render_scene.view = {};
    m_has_camera = false;

    for (const auto [entity, world, camera]:
            scene.view<scene::WorldTransformComponent, CameraComponent>().each()) {
        if (!camera.primary) {
            continue;
        }
        if (target.width == 0 || target.height == 0) {
            break;
        }

        const auto aspect = static_cast<float>(target.width) / static_cast<float>(target.height);
        m_render_scene.view.view = glm::affineInverse(world.world);
        m_render_scene.view.projection = glm::perspectiveRH_ZO(glm::radians(camera.fov_degrees),
                aspect, camera.near_plane, camera.far_plane);
        m_render_scene.view.camera_position = glm::vec3{ world.world[3] };
        m_has_camera = true;
        break;
    }

    for (const auto [entity, world, light]:
            scene.view<scene::WorldTransformComponent, DirectionalLightComponent>().each()) {
        if (!light.enabled) {
            continue;
        }
        rendering::LightDesc desc;
        desc.type = rendering::LightType::Directional;
        desc.direction = forwardOf(world.world);
        desc.color = glm::vec4{ light.color, 1.0f };
        desc.intensity = light.intensity;
        m_render_scene.lights.push_back(desc);
    }

    for (const auto [entity, world, mesh_renderer, id]:
            scene.view<scene::WorldTransformComponent, MeshRendererComponent, scene::IDComponent>().each()) {
        if (!mesh_renderer.visible || !mesh_renderer.mesh.isValid()) {
            continue;
        }


        const auto mesh = assets.meshHandle(mesh_renderer.mesh.id());
        if (!mesh.isValid()) {
            continue;
        }

        rendering::DrawItem draw;
        draw.mesh = mesh;
        draw.submesh_index = mesh_renderer.submesh_index;
        // 按 submesh 取对应槽的材质。数组比槽短或者那一项无效时留空句柄 ——
        // 资源注册表会回退到默认材质，而不是整个不画。
        if (mesh_renderer.submesh_index < mesh_renderer.materials.size()) {
            draw.material = assets.materialHandle(
                    mesh_renderer.materials[mesh_renderer.submesh_index].id());
        }
        draw.transform = world.world;
        draw.world_bounds = worldBounds(renderer, mesh, world.world);
        draw.picking_id = pickingIdFor(id.id);
        m_render_scene.draws.push_back(draw);
    }

    return m_render_scene;
}

uint32_t RenderSceneExtractor::pickingIdFor(core::UUID entity) {
    const auto existing = m_picking_ids.find(entity);
    if (existing != m_picking_ids.end()) {
        return existing->second;
    }

    const uint32_t id = m_next_picking_id++;
    m_picking_ids.emplace(entity, id);
    m_picking_entities.emplace(id, entity);
    return id;
}

std::optional<core::UUID> RenderSceneExtractor::entityForPickingId(uint32_t picking_id) const {
    if (picking_id == 0) {
        return std::nullopt;
    }
    const auto found = m_picking_entities.find(picking_id);
    return found == m_picking_entities.end() ? std::nullopt : std::optional{ found->second };
}

rendering::AABB RenderSceneExtractor::worldBounds(const rendering::Renderer& renderer,
        rendering::MeshHandle mesh, const glm::mat4& world) {
    auto cached = m_mesh_bounds.find(mesh);
    if (cached == m_mesh_bounds.end()) {
        const auto info = renderer.meshInfo(mesh);
        cached = m_mesh_bounds.emplace(mesh, info ? info->bounds : rendering::AABB{}).first;
    }
    return cached->second.transformed(world);
}

} // namespace arti::engine
