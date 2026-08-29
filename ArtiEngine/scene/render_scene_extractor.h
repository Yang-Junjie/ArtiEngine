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
    // 上传之后顶点数据已经不在 CPU 侧，submesh 数量和包围盒只能问 Renderer。按网格缓存，
    // 因为同一个网格常被多个实体引用。
    rendering::MeshInfo meshInfo(const rendering::Renderer& renderer, rendering::MeshHandle mesh);

    uint32_t pickingIdFor(core::UUID entity);

    rendering::RenderScene m_render_scene;
    std::unordered_map<rendering::MeshHandle, rendering::MeshInfo> m_mesh_info;

    std::unordered_map<core::UUID, uint32_t> m_picking_ids;
    std::unordered_map<uint32_t, core::UUID> m_picking_entities;
    uint32_t m_next_picking_id{ 1 };

    bool m_has_camera{ false };
};

} // namespace arti::engine
