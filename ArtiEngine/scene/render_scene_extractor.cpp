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
        asset::GPUAssetCache& assets, const rendering::Renderer& renderer, ExtractTarget target) {
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
            scene.view<scene::WorldTransformComponent, MeshRendererComponent, scene::IDComponent>()
                    .each()) {
        if (!mesh_renderer.visible || !mesh_renderer.mesh.isValid()) {
            continue;
        }


        const auto mesh = assets.meshHandle(mesh_renderer.mesh.id());
        if (!mesh.isValid()) {
            continue;
        }

        const rendering::MeshInfo info = meshInfo(renderer, mesh);
        // 一个 MeshRendererComponent 画整个网格：每个 submesh 一个 draw。这是 materials 是
        // 数组而不是单个句柄的原因 —— 下标就是槽位。
        //
        // 所有 draw 共用同一个 picking_id：submesh 是网格的内部结构、不是场景里的东西，
        // 点到哪一段都应该选中这个实体。
        const uint32_t picking_id = pickingIdFor(id.id);
        // MeshInfo::bounds 是整个网格的盒子而不是单个 submesh 的（见 mesh.h 的说明），
        // 所以同一个网格的所有 submesh 共用它。对视锥剔除是偏保守的方向：宁可多画不能漏画。
        const rendering::AABB bounds = info.bounds.transformed(world.world);

        for (uint32_t submesh = 0; submesh < info.submesh_count; ++submesh) {
            rendering::DrawItem draw;
            draw.mesh = mesh;
            draw.submesh_index = submesh;
            // 数组比槽位短或者那一项无效时留空句柄 —— 资源注册表会回退到默认材质，
            // 而不是整个不画。
            if (submesh < mesh_renderer.materials.size()) {
                draw.material = assets.materialHandle(mesh_renderer.materials[submesh].id());
            }
            draw.transform = world.world;
            draw.world_bounds = bounds;
            draw.picking_id = picking_id;
            m_render_scene.draws.push_back(draw);
        }
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

rendering::MeshInfo RenderSceneExtractor::meshInfo(const rendering::Renderer& renderer,
        rendering::MeshHandle mesh) {
    auto cached = m_mesh_info.find(mesh);
    if (cached == m_mesh_info.end()) {
        const auto info = renderer.meshInfo(mesh);
        cached = m_mesh_info.emplace(mesh, info ? *info : rendering::MeshInfo{}).first;
    }
    return cached->second;
}

} // namespace arti::engine
