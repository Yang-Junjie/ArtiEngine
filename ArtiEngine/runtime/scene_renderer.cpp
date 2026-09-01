#include "runtime/scene_renderer.h"

#include "asset/gpu_asset_cache.h"

namespace arti::engine {

SceneRenderer::SceneRenderer(rendering::Renderer& renderer) noexcept
        : m_renderer(renderer) {}

bool SceneRenderer::prepare(scene::Scene& scene, asset::GPUAssetCache* assets,
        const ViewportInfo& viewport) {
    // 无条件设：IntoUI 模式下面板尺寸变了要立刻跟上，哪怕这一帧没东西可画。
    // 0 在渲染器那边就是「跟着输出走」的意思。
    m_renderer.setSceneTargetSize(viewport.target_follows_output ? 0 : viewport.width,
            viewport.target_follows_output ? 0 : viewport.height);
    m_has_scene = false;

    if (assets == nullptr || viewport.width == 0 || viewport.height == 0) {
        return false;
    }

    ExtractTarget target;
    target.width = viewport.width;
    target.height = viewport.height;
    m_extractor.extract(scene, *assets, m_renderer, target);

    if (viewport.view_override) {
        // 覆盖在 extract 之后：它要盖掉场景里的 primary 相机（如果有），而不是被它盖掉。
        m_extractor.overrideView(*viewport.view_override);
    } else if (!m_extractor.hasCamera()) {
        return false;
    }

    m_has_scene = true;
    return true;
}

rendering::FrameStatistics SceneRenderer::submit(const rendering::FrameOverlay& overlay) {
    const rendering::RenderScene empty;
    const rendering::FrameStatistics statistics =
            m_renderer.renderFrame(m_has_scene ? m_extractor.renderScene() : empty, overlay);
    m_has_scene = false;
    return statistics;
}

} // namespace arti::engine
