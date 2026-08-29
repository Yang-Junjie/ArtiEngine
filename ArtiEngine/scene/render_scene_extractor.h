#pragma once
#include "arti_renderer.h"

#include <cstdint>

#include <optional>
#include <unordered_map>

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::engine::asset {
class GPUAssetCache;
} // namespace arti::engine::asset

namespace arti::engine {

struct ExtractTarget {
    uint32_t width{ 0 };
    uint32_t height{ 0 };
};
class RenderSceneExtractor {
public:
    const rendering::RenderScene& extract(scene::Scene& scene, asset::GPUAssetCache& assets,
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

    uint32_t pickingIdFor(core::UUID entity);

    rendering::RenderScene m_render_scene;
    std::unordered_map<rendering::MeshHandle, rendering::AABB> m_mesh_bounds;

    std::unordered_map<core::UUID, uint32_t> m_picking_ids;
    std::unordered_map<uint32_t, core::UUID> m_picking_entities;
    uint32_t m_next_picking_id{ 1 };

    bool m_has_camera{ false };
};

} // namespace arti::engine
