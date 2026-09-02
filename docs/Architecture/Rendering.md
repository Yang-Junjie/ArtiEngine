# 渲染

> 本页讲从 `RenderScene` 到画面这一段：三层职责、管线的 stage 顺序及其理由、两种呈现模式、
> 拾取和调试线。Vulkan / NVRHI 边界、Slang 反射、坐标约定和线程模型在
> `ArtiRenderer/ArtiChoco/artichoco/renderer/README.md`。

## 1. 三层

```
engine::SceneRenderer          抽取 + 提交的固定顺序，「没有可画的场景」的策略
        │
rendering::Renderer            资源句柄、延迟管线、拾取、调试线、两种呈现模式
        │
renderer::RenderDevice         Vulkan bootstrap、NVRHI 设备、swapchain、帧管理、Slang 编译
```

`Renderer` **不拥有** `RenderDevice` —— 谁建窗口和 surface 谁建 device。渲染只有一个线程：
建 / 拆 `RenderDevice`、建资源、录 pass、请求重建 swapchain、`waitIdle` 都必须在它上面。
工作线程可以做文件 IO、解压、图片解码，然后把数据交给渲染线程上传。

## 2. RenderScene：唯一的契约

`ArtiRenderer/ArtiRenderer/include/render_scene.h`。引擎往渲染器递的全部东西：

```cpp
struct RenderScene {
    RenderView              view;         // view / projection / camera_position
    std::vector<DrawItem>   draws;        // mesh + submesh + material + transform
                                          //   + world_bounds + picking_id
    std::vector<LightDesc>  lights;       // Directional / Point / Spot 混在一起
    EnvironmentDesc         environment;  // 一份，不是列表
    glm::vec4               clear_color;
    float                   exposure;     // 线性倍率，tone mapping 之前乘上去
};
```

`exposure` 和 `clear_color` 放在场景上而不是相机上，是同一个取舍：这两个都是「这一帧怎么
呈现」的旋钮，不是场景内容。真做多相机、每相机自己曝光时再搬进 `RenderView`。

调试线和拾取请求**不在** `RenderScene` 里：它们是「攒到下一帧」的形状，累积在 `Renderer`
上，每帧快照进 `FrameContext`。调试线不是场景内容。

## 3. SceneRenderer：prepare / submit 两段

`ArtiEngine/runtime/scene_renderer.h`。

```cpp
bool prepare(scene::Scene&, asset::GPUAssetCache*, const ViewportInfo&);
rendering::FrameStatistics submit(const rendering::FrameOverlay& = {});
```

**刻意分成两段而不是一个 `renderFrame()`**：调试线（选中轮廓、光源线框）必须在抽取**之后**
提交（要用这一帧的相机），又必须在 `renderFrame` **之前**提交（它们只作用于紧接着的那一帧）。
两段之间就是那个窗口。

`prepare()` 返回 `false` 表示这一帧没有可画的场景：`assets` 为 null（工作区没开）、目标尺寸
为 0（面板还没布局好或窗口最小化）、或者场景里既没有 primary 相机也没给覆盖相机。
`assets` 允许为 null 就是为了让调用方能无条件走这条路 —— `submit()` 必须每帧都到，
它是唯一提交 UI overlay 的地方，early return 掉就是整个界面黑屏。

`submit()` 在 `prepare()` 返回过 `false`（或者压根没调）时提交一个空场景：没有 draw，
只有 overlay。这条策略在编辑器和运行时是同一个决定，所以收在这里而不是各写一遍。

`ViewportInfo` 里两个开关值得注意：

- `view_override` —— 编辑器在 Edit 模式下用编辑器相机，盖掉场景里的 primary 相机。
- `target_follows_output` —— 让渲染目标跟着渲染输出走而不是照 `width/height` 建。运行时该开：
  真正的输出尺寸在提交时才由 swapchain 定下来，比调用方查到的 `outputInfo()` 更权威 ——
  缩放窗口时后者会差一帧，照它建目标就是一次多余的重建加一次缩放采样。宽高比仍然取
  `width/height`，因为抽取发生在提交之前，那时拿不到解析后的尺寸。

## 4. 延迟管线

只有 `PipelineKind::Deferred` 一条路径 —— 前向管线已整条移除，不留双路径。

整条链作为**一个** `renderer::RenderPass` 提交给 `RenderDevice::renderFrame`，而不是每个
pass 包一层 adapter。这样 `RenderTargetSet::prepare()` 有了确定位置（在所有 pass 的
`prepare()` 之前），pass 里拿到的目标一定建好了。

### stage 顺序

执行顺序就是 `addPass` 的调用顺序；`LinearStage` 不参与排序，它的作用是让「谁必须排在谁
前面」变成可机检的声明 —— 装错位置在安装时就抛，而不是等画面不对再查。

| stage | pass | 干什么 / 为什么在这个位置 |
| --- | --- | --- |
| `EnvironmentBake` | `EnvironmentBakePass` | 纯 compute，不碰 framebuffer。IBL 烘焙。排最前是因为产物是 Lighting / Sky 的输入，而它自己不依赖任何渲染目标。按环境贴图句柄缓存，只有换贴图那帧真的烘 |
| `Shadow` | `ShadowPass` | 方向光的级联阴影深度图：把几何按每一级的光源正交视锥重画一遍，只写深度。和 `EnvironmentBake` 同一个性质（产物是 Lighting 的输入、自己不依赖场景目标），所以排在它之后、`Clear` 之前 —— 那样「谁清场景目标」仍然只有 `ClearScenePass` 一处。没有投影光源或没有 draw 的帧整个跳过 |
| `Clear` | `ClearScenePass` | 独立成 pass 而不是让第一个绘制 pass 顺手清屏 —— 靠约定就成了隐式耦合，改 pass 顺序会静默改变清屏行为 |
| `GBuffer` | `GBufferPass` | 把材质属性编码进 G-Buffer + SceneDepth，一行光照都不算 |
| `Lighting` | `DeferredLightingPass` | 全屏三角形读 G-Buffer + 深度，写 SceneColor。**整条管线里唯一求值 BRDF 的地方**，也是唯一采阴影图的地方（只作用在那个投影方向光的直接光项上，不乘 IBL） |
| `Sky` | `SkyPass` | 排在 Lighting 之后：深度测试 LessOrEqual + 不写深度，只填没被物体覆盖的像素。反过来先画天空，每个被挡住的像素都白画一遍 |
| `Picking` | `PickingPass` | 复用可见性做 ID 缓冲。排在 PostProcess 之前是因为它读深度、跟颜色无关，而读回是异步的 —— 早提交早能取。没有拾取请求的帧连 ID 缓冲都不建 |
| `PostProcess` | `TonemapPass` | 曝光 + ACES 拟合曲线（Narkowicz 2015），SceneColor（场景线性 HDR）→ DisplayColor（显示线性 LDR）。**不做 sRGB 编码**，那个由硬件在写 backbuffer 时完成 |
| `DebugOverlay` | `DebugLinePass` | 画在 DisplayColor 上（tone mapping **之后**），所以「我给的颜色就是我看到的颜色」。深度测试复用 SceneDepth，被物体挡住的线看不见。没有线的帧连 shader 都不编译 |
| `Output` | `PresentPass` | 把 DisplayColor 贴到 backbuffer。`IntoUI` 模式下整个跳过 |
| `UI` | `ImGuiPass` | 直接画进 backbuffer 而不是 SceneColor：UI 因此永远是原生分辨率，也不受场景侧后处理影响。没有 draw data 的帧不生效 |

**几何 pass 拆分的依据是 G-Buffer 编码，不是材质类型。** 延迟管线里着色模型已经统一到
Lighting，同一种编码再分几个 pass 只是几份一样的 PSO。所以现在只需要一个几何 pass；
加透明 / 蒙皮 / 地形时才有拆的理由。

### 跨 pass 的资源

pass 之间互不认识，靠三个管线拥有的具名结构交接。都带 `revision()`，下游靠它判断要不要
重建 binding set。

| 结构 | 内容 | 写 → 读 |
| --- | --- | --- |
| `RenderTargetSet` | `SceneColor`（RGBA16F，场景线性 HDR）、`SceneDepth`、`DisplayColor`（RGBA8，显示线性 LDR）、本帧 backbuffer | Clear/GBuffer/Lighting/Sky → Tonemap → Present/ImGui |
| `GBufferTargets` | `albedo_metallic`（SRGBA8：rgb=albedo，a=metallic）、`normal_roughness`（RGBA16F：xyz=世界法线，w=roughness）、`emissive_occlusion`（RGBA16F：rgb=emissive，a=occlusion） | GBufferPass → DeferredLightingPass |
| `ShadowTargets` | 一张 `Texture2DArray` 的 D32 深度图（4 级 × 2048²）+ 每级一个 framebuffer，外加这一帧的 cascade 矩阵、分割距离和投影光源下标 | ShadowPass → DeferredLightingPass |
| `EnvironmentResources` | `environment` cube（带 mip 链）、`irradiance`（32²）、`prefiltered`（mip ↔ roughness）、`brdf_lut`（split-sum，全局只烘一次） | EnvironmentBakePass → Lighting / Sky |

三个都是**具名字段而不是泛型 slot 表**。`RenderTargetSet` 的名字承载的是色彩空间契约
（scene 线性 / display 线性 / backbuffer sRGB），泛型槽谁都能造个新目标，这层含义就没了。
G-Buffer 和 IBL 产物另起结构而不是塞进它，也是同一个理由：法线和 roughness 谈不上色彩空间，
IBL 那几张也不是 render target。

两条相关的细节：

- **`SceneDepth` 一张两用**：G-Buffer 借它当深度附件，`DeferredLightingPass` 随后把它当 SRV
  采样反投影出世界坐标。同一张图不能同时是深度附件和 SRV（Vulkan 的 feedback loop），
  所以光照用的是只挂 SceneColor 的那个 framebuffer。
- **`PickingPass` 自己带一张深度**，不借 SceneDepth：它要的是「深度相等」比较，那要求两个
  pass 的顶点变换逐位一致，靠不住。
- **IBL 不可用时那几个句柄指向 1×1 黑色兜底资源，不是空句柄** —— 否则 binding set 建不起来。
  着色端看到 `ready == false` 就回落到常数环境项。

### 着色器

`ArtiRenderer/ArtiRenderer/src/shaders/`，全部 Slang，编译成 SPIR-V；**Slang 的反射是绑定
布局的唯一权威**（descriptor set → NVRHI `registerSpace`，常量缓冲 / 纹理 / 采样器 /
push constant 都按反射映射）。

```
gbuffer.slang           几何写入
shadow_depth.slang      级联阴影深度（只写深度，fragmentMain 是空的）
deferred_lighting.slang BRDF 求值（唯一）
skybox.slang            天空
equirect_to_cube.slang  等距柱状 → cube
irradiance.slang        余弦卷积
prefilter.slang         GGX 预滤波
brdf_lut.slang          split-sum 积分项
ibl_common.slang        上面几个共用
tonemap.slang           曝光 + ACES 拟合
present.slang           贴 backbuffer
picking.slang           ID 缓冲
debug_line.slang        调试线
imgui.slang             UI
```

从源码树按绝对路径读（`ARTIRENDERER_SHADER_DIR`），改 shader 不用重编 C++。

## 5. 两种呈现模式

`PresentMode` 运行时可切 —— 编辑器里在「全屏预览」和「场景进面板」之间来回是常见操作，
不值得为它重建一条管线。

| 模式 | 谁用 | 行为 |
| --- | --- | --- |
| `Direct` | 播放器 | `PresentPass` 把 DisplayColor 贴到 backbuffer，UI（如果有）盖在上面 |
| `IntoUI` | 编辑器 | `PresentPass` 关掉，场景留在离屏纹理里，宿主用 `ImGui::Image(sceneColorTextureId(), …)` 放进 Viewport 面板；这一帧的 backbuffer 完全由 UI 拥有 |

`IntoUI` 模式下宿主应该按面板的像素尺寸调 `setSceneTargetSize()`，否则场景会按窗口尺寸渲染
再被缩放。

**tone mapping 独立成 pass 而不是塞进 `PresentPass`**，正是因为 `IntoUI` 下 `PresentPass`
根本不跑：压缩必须发生在一张纹理里，两条呈现路径才能看到同一个画面。调试线排在 tonemap
之后、`Output` 之前，也是为了让两条路径都看到带调试线的画面。

## 6. GPU 拾取

```cpp
renderer.requestPick({x, y});                 // renderFrame 之前调，只作用于紧接着那一帧
// …若干帧之后…
if (auto pick = renderer.takePickResult()) {  // 取走即清空
    auto entity = scene_renderer.entityForPickingId(pick->picking_id);  // 0 = 点在空处
}
```

坐标是**场景渲染目标内**的像素（左上原点），编辑器模式下就是 Viewport 面板内的位置 ——
不是窗口坐标。`PickingPass` 复用 GBuffer 阶段的可见性做 LessOrEqual 测试，所以「点到的」和
「看到的」永远一致。读回是异步的，调用方每帧问一次即可，不要阻塞等它。

## 7. 调试绘制

```cpp
renderer.drawLine(from, to, color);
renderer.drawAABB(world_bounds, color);        // 12 条边，世界空间
renderer.drawWireSphere(center, radius, color, segments = 24);
```

和 `requestPick()` 一样是「攒到下一帧」的形状：只画紧接着那一帧，`renderFrame()` 消费完就
清空。调用方不用管生命周期，也不用把一个列表层层传下来。

画在 tone mapping 之后，所以 `color` 写什么就看到什么。深度测试是开的 —— 线在世界里是有
位置的。编辑器用它画选中实体的轮廓（`drawAABB`）和点光源的 `range`（`drawWireSphere`）。

## 8. FrameStatistics

```cpp
uint32_t draw_calls;  // 场景里画了多少个 submesh。全屏 pass 和调试线不计入
uint32_t submeshes;
uint32_t culled;      // 恒为 0 —— 剔除还没做
bool     rendered;
```
