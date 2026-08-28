#pragma once
#include "arti_renderer.h"

#include <cstdint>

#include <optional>
#include <unordered_map>

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::engine {

struct ExtractTarget {
    uint32_t width{ 0 };
    uint32_t height{ 0 };
};
class RenderSceneExtractor {
public:
    // renderer 只用来读网格的局部包围盒（Renderer::meshInfo）
    const rendering::RenderScene& extract(scene::Scene& scene, const rendering::Renderer& renderer,
            ExtractTarget target);

    const rendering::RenderScene& renderScene() const noexcept { return m_render_scene; }

    void overrideView(const rendering::RenderView& view) noexcept {
        m_render_scene.view = view;
        m_has_camera = true;
    }
    bool hasCamera() const noexcept { return m_has_camera; }

    // GPU 拾取拿到 picking_id 之后反查实体。查不到返回空：点在空处（id 0），
    // 或者那个实体在读回途中被删了。
    std::optional<core::UUID> entityForPickingId(uint32_t picking_id) const;

private:
    rendering::AABB worldBounds(const rendering::Renderer& renderer, rendering::MeshHandle mesh,
            const glm::mat4& world);

    // 给实体分配拾取编号，已经有的就返回原来那个。
    //
    // 只增不复用，而且跨帧稳定 —— 这是拾取读回是异步的必然要求：请求发出去几帧之后结果才
    // 回来，逐帧重编号的话那时编号表已经变了，会选中错的实体。
    //
    // 也没有拿 UUID 低 32 位截断：UUID 是随机 64 位，截断到 32 位在几万个实体量级就会撞，
    // 而撞了的表现是「点 A 选中 B」，极难查。顺序发号从根上没有这个问题。
    uint32_t pickingIdFor(core::UUID entity);

    rendering::RenderScene m_render_scene;
    std::unordered_map<rendering::MeshHandle, rendering::AABB> m_mesh_bounds;

    // 双向表。正向给 DrawItem 填号，反向给拾取结果反查。
    std::unordered_map<core::UUID, uint32_t> m_picking_ids;
    std::unordered_map<uint32_t, core::UUID> m_picking_entities;
    // 0 是「空处」的保留值，所以从 1 开始发。
    uint32_t m_next_picking_id{ 1 };

    bool m_has_camera{ false };
};

} // namespace arti::engine
