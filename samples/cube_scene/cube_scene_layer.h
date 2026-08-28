#pragma once
#include "arti_engine.h"
#include "artichoco/core/layer.h"

#include <cstdint>

#include <memory>

namespace arti::renderer {
class RenderDevice;
} // namespace arti::renderer

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::sample {

// 最小的 ECS 驱动样例：实体 + 组件 -> RenderSceneExtractor -> rendering::Renderer。
// 和 ArtiRenderer 的 basic_window 的区别就是 RenderScene 不再是手搓的，而是从场景抽出来的。
class CubeSceneLayer final : public core::Layer {
public:
    CubeSceneLayer(bool enable_renderer, uint32_t frame_limit);
    ~CubeSceneLayer() override;

    void onAttach() override;
    void onDetach() override;
    void onUpdate(core::Timestep delta_time) override;
    void onRender() override;

private:
    void createSceneEntities();

    bool m_enable_renderer{ false };
    uint32_t m_frame_limit{ 0 };
    uint32_t m_frame_index{ 0 };
    float m_elapsed_seconds{ 0.0f };

    std::unique_ptr<renderer::RenderDevice> m_render_device;
    std::unique_ptr<rendering::Renderer> m_renderer;
    std::unique_ptr<scene::Scene> m_scene;
    engine::RenderSceneExtractor m_extractor;

    // 转起来的那几个立方体，onUpdate 里直接改它们的 TransformComponent。
    std::vector<core::UUID> m_spinning;

    rendering::MeshHandle m_cube_mesh;
    rendering::MaterialHandle m_cube_material;
    rendering::TextureHandle m_checker_texture;
};

} // namespace arti::sample
