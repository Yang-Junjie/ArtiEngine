#pragma once
#include "arti_renderer.h"

#include <cstdint>

#include <unordered_map>

namespace arti::scene {
class Scene;
} // namespace arti::scene

namespace arti::engine {

// 场景渲染到多大。相机的投影需要宽高比，而宽高比是目标的属性 —— Direct 模式下是窗口，
// 编辑器模式下是 Viewport 面板，所以这个值只有调用方知道，必须传进来。
struct ExtractTarget {
    uint32_t width{ 0 };
    uint32_t height{ 0 };
};

// 把 ECS 场景翻译成 ArtiRenderer 的 RenderScene。这是 ArtiEngine 存在的理由：
// 下面两层已经各自完整（ArtiChoco 管 ECS 和 RHI，ArtiRenderer 管画），缺的就是这道翻译。
//
// 刻意**不是** scene::SceneSystem。SceneSystem 的 onUpdate(Scene&, UpdateContext&) 没有输出通道，
// 要当系统用就得「先 setTarget、跑一遍、再把 RenderScene 取出来」——
// 那正是我们在 aspect 上拒绝的那种「需要每帧同步的可变状态」。
// 这里做成 (scene, target) -> RenderScene 的纯函数，没有隐藏状态，也好测。
//
// SystemStage::RenderExtract 仍然留给真正需要跑在 ECS 更新管线里的东西 —— 比如动画必须在
// extract 之前收尾，剔除想作为独立系统跑。
class RenderSceneExtractor {
public:
    // renderer 只用来读网格的局部包围盒（Renderer::meshInfo）——
    // 顶点数据上传后就不在 CPU 侧了，DrawItem::world_bounds 只能这么算出来。
    //
    // 内部会先调 scene.updateWorldTransforms()：ArtiChoco 那个不是自动的，漏调就会画出
    // 「物体动了但画面差一帧」这种极难查的问题。放在这里而不是当前置条件写进注释，
    // 是因为不可能忘。
    const rendering::RenderScene& extract(scene::Scene& scene, const rendering::Renderer& renderer,
            ExtractTarget target);

    // 上一次 extract 的结果。
    const rendering::RenderScene& renderScene() const noexcept { return m_render_scene; }

    // 场景里一个标了 primary 的相机都没有时为 false。此时 RenderScene 的 view 是单位矩阵，
    // 画出来没有意义，调用方通常应该整帧跳过。
    bool hasCamera() const noexcept { return m_has_camera; }

private:
    // 网格局部包围盒变换到世界空间，结果进 DrawItem::world_bounds。查表结果会缓存。
    rendering::AABB worldBounds(const rendering::Renderer& renderer, rendering::MeshHandle mesh,
            const glm::mat4& world);

    // 每帧复用，不重新构造 —— draws / lights 的容量因此能留住。
    rendering::RenderScene m_render_scene;
    // MeshHandle -> 局部包围盒。网格上传后包围盒就不变了，所以可以一直缓存，
    // 省掉每帧每 draw 一次 meshInfo() 查表。
    std::unordered_map<rendering::MeshHandle, rendering::AABB> m_mesh_bounds;
    bool m_has_camera{ false };
};

} // namespace arti::engine
