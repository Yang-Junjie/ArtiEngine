#include "render_scene_extractor.h"

#include "artichoco/scene/components.h"
#include "artichoco/scene/scene.h"
#include "components.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace arti::engine {
namespace {

// 世界变换的 -Z 轴，也就是光的传播方向（从光源射出）。
// rendering::LightDesc::direction 要的正是这个朝向，pass 那边会取反。
glm::vec3 forwardOf(const glm::mat4& world) noexcept {
    const glm::vec3 forward{ -world[2] };
    const float length = glm::length(forward);
    // 缩放为 0 的实体会给出零向量，归一化会得到 NaN。退化时给个确定的朝下方向。
    return length > 0.0f ? forward / length : glm::vec3{ 0.0f, -1.0f, 0.0f };
}

} // namespace

const rendering::RenderScene& RenderSceneExtractor::extract(scene::Scene& scene,
        ExtractTarget target) {
    // ArtiChoco 的世界变换是惰性的，必须显式刷。
    scene.updateWorldTransforms();

    // clear 而不是重新构造：容量留住，稳定下来之后每帧零分配。
    m_render_scene.draws.clear();
    m_render_scene.lights.clear();
    m_render_scene.view = {};
    m_has_camera = false;

    // 相机。aspect 从目标尺寸算，不从组件读 —— 见 CameraComponent 的注释。
    for (const auto [entity, world, camera]:
            scene.view<scene::WorldTransformComponent, CameraComponent>().each()) {
        if (!camera.primary) {
            continue;
        }
        // 目标退化（窗口最小化、面板折叠）时不要算出 inf/NaN 的投影，整帧交给调用方跳过。
        if (target.width == 0 || target.height == 0) {
            break;
        }

        const auto aspect = static_cast<float>(target.width) / static_cast<float>(target.height);
        // view 是世界变换的逆。用 affineInverse 而不是通用 inverse：变换是仿射的，
        // 这样又快又稳。
        m_render_scene.view.view = glm::affineInverse(world.world);
        // RH_ZO：深度范围 [0,1]，和 ArtiRenderer 的 pass 约定一致。
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

    for (const auto [entity, world, renderer]:
            scene.view<scene::WorldTransformComponent, MeshRendererComponent>().each()) {
        if (!renderer.visible || !renderer.mesh.isValid()) {
            continue;
        }
        rendering::DrawItem draw;
        draw.mesh = renderer.mesh;
        draw.submesh_index = renderer.submesh_index;
        draw.material = renderer.material;
        draw.transform = world.world;
        // world_bounds 暂时留空。填它需要网格的局部 AABB，而那存在 Renderer 的资源注册表里
        // （GPUMesh::bounds），公开 API 现在拿不到 —— 要加个 Renderer::meshInfo() 之类的东西。
        // 视锥剔除做之前必须先补上这个，否则剔除没有输入。
        m_render_scene.draws.push_back(draw);
    }

    return m_render_scene;
}

} // namespace arti::engine
