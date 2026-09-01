#pragma once
#include "scene/render_scene_extractor.h"

#include <cstdint>
#include <optional>

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::engine {

namespace asset {
class GPUAssetCache;
} // namespace asset

// 一帧场景到屏幕。包住 RenderSceneExtractor，把「抽取」和「提交」之间的固定顺序，以及
// 「这一帧没有可画的场景就提交空场景」这条策略收在一处 —— 后者在编辑器和运行时是同一个决定。
//
// 刻意分成 prepare / submit 两段，而不是一个 renderFrame()：调试线（选中轮廓、光源线框）
// 必须在 extract 之后提交（要用这一帧的相机），又必须在 renderFrame 之前提交（它们只作用于
// 紧接着的那一帧）。两段之间就是那个窗口。
class SceneRenderer {
public:
    explicit SceneRenderer(rendering::Renderer& renderer) noexcept;

    struct ViewportInfo {
        // 场景画多大，同时也是投影矩阵宽高比的来源。任一维为 0 表示这一帧没有可画的场景
        // （面板还没布局好、或者窗口最小化）。
        std::uint32_t width{ 0 };
        std::uint32_t height{ 0 };
        // 让渲染目标跟着渲染输出走，而不是照 width / height 建。运行时该开：真正的输出尺寸
        // 在提交时才由 swapchain 定下来，比调用方查到的 outputInfo() 更权威 —— 缩放窗口时
        // 后者会差一帧，照它建目标就是一次多余的重建加一次缩放采样。
        //
        // 宽高比仍然取 width / height：抽取发生在提交之前，那时拿不到解析后的尺寸。
        bool target_follows_output{ false };
        // 覆盖相机。编辑器在 Edit 模式下用编辑器相机；nullopt 表示用场景里的 primary 相机。
        std::optional<rendering::RenderView> view_override{};
    };

    // 抽取这一帧的场景。返回 false 表示没有可画的场景：assets 为 null（工作区没开）、
    // 目标尺寸为 0（面板还没布局好或窗口最小化）、或者场景里没有 primary 相机又没给覆盖相机。
    //
    // assets 允许为 null 就是为了让调用方能无条件走这条路 —— submit() 必须每帧都到，
    // 它是唯一提交 UI overlay 的地方，early return 掉就是整个界面黑屏。
    bool prepare(scene::Scene& scene, asset::GPUAssetCache* assets, const ViewportInfo& viewport);

    // 提交这一帧。prepare() 返回过 false（或者这一帧压根没调 prepare）时提交一个空场景：
    // 没有 draw，只有 overlay。
    rendering::FrameStatistics submit(const rendering::FrameOverlay& overlay = {});

    const rendering::RenderScene& renderScene() const noexcept { return m_extractor.renderScene(); }

    // 上一次 prepare() 抽出来的场景里有没有相机（含覆盖相机）。UI 靠它解释「为什么没画」，
    // 读到的是上一帧的结论 —— UI 在 prepare 之前画，这一点在编辑器里一直如此。
    bool hasCamera() const noexcept { return m_extractor.hasCamera(); }

    std::optional<core::UUID> entityForPickingId(std::uint32_t picking_id) const {
        return m_extractor.entityForPickingId(picking_id);
    }

private:
    rendering::Renderer& m_renderer;
    RenderSceneExtractor m_extractor;
    // 这一帧有没有抽出可画的场景。submit() 消费完就清掉，所以「没调 prepare」和
    // 「prepare 失败」是同一个结果，不会残留上一帧的场景。
    bool m_has_scene{ false };
};

} // namespace arti::engine
