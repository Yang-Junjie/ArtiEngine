#pragma once
#include "arti_renderer.h"

#include <cstdint>

#include <optional>
#include <unordered_map>

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::engine::asset {
class GpuAssetCache;
} // namespace arti::engine::asset

namespace arti::engine {

struct ExtractTarget {
    uint32_t width{ 0 };
    uint32_t height{ 0 };
};
class RenderSceneExtractor {
public:
    // assets 把组件里的资产引用解析成 renderer 句柄；renderer 用来读网格的局部包围盒。
    // 两个都传是因为职责不同：一个管「资产 -> GPU」，一个管「GPU 资源的属性」。
    const rendering::RenderScene& extract(scene::Scene& scene, asset::GpuAssetCache& assets,
            const rendering::Renderer& renderer, ExtractTarget target);

    const rendering::RenderScene& renderScene() const noexcept { return m_render_scene; }

    void overrideView(const rendering::RenderView& view) noexcept {
        m_render_scene.view = view;
        m_has_camera = true;
    }
    bool hasCamera() const noexcept { return m_has_camera; }

    std::optional<core::UUID> entityForPickingId(uint32_t picking_id) const;

private:
    rendering::AABB worldBounds(const rendering::Renderer& renderer, rendering::MeshHandle mesh,
            const glm::mat4& world);

    // 只增不复用：拾取读回是异步的，逐帧重编号会让晚几帧到的结果选中错的实体。
    uint32_t pickingIdFor(core::UUID entity);

    rendering::RenderScene m_render_scene;
    std::unordered_map<rendering::MeshHandle, rendering::AABB> m_mesh_bounds;

    std::unordered_map<core::UUID, uint32_t> m_picking_ids;
    std::unordered_map<uint32_t, core::UUID> m_picking_entities;
    uint32_t m_next_picking_id{ 1 };

    bool m_has_camera{ false };
};

} // namespace arti::engine
