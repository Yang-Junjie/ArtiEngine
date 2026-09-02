# 方向光级联阴影（CSM）

| | |
| --- | --- |
| **状态** | 已完成并验收、已 push（7.1 级间混合明确不做）|
| **创建** | 2026-09-02 |
| **最后更新** | 2026-09-02 |
| **涉及仓库** | ArtiRenderer（主要）、ArtiEngine（光源组件与序列化） |
| **目标** | 方向光投射级联阴影：4 级、拟合相机视锥、texel snapping、3×3 PCF、slope-scaled bias、shadow distance 截断 |

---

## 交接区

> **全文唯一允许改动的段落。每次收工前更新这里。**

**当前进度**：**功能全部完成并已由用户在编辑器里验过**（2026-09-02）。25 步里 24 步打勾，
只剩 7.1（级间混合，可选、明确不做）。

**用户验过的三条**（我没有交互控制相机 / 光源的手段，交给用户看的）：
1. shimmering —— 平移相机，边缘不闪。
2. 掠射角 acne —— 转动方向光到接近平行地面，无摩尔纹。当前 bias（`slopeScale=2.0, constant=1`）
   在掠射角下也够。
3. cascade 选级 —— 无接缝、无伪影。

**下一步**：只剩 **push**（三层，由内向外）：

```
git -C ArtiRenderer/ArtiChoco push   # 本任务没动它，可跳过
git -C ArtiRenderer push
git push
```

**7.1 明确不做**：Godot 的 `blend_splits` 默认也是关的，级间接缝作为已知项记在
`docs/Architecture/README.md` 的缺口表里。真觉得碍眼再单独开一份任务。

**需要你帮忙验的三条**（我只能启动 / 抓图 / 关窗，没有交互控制相机和光源的手段）：
1. **5.3 shimmering**：编辑器里平移相机，看阴影边缘会不会闪。间接证据是四级的
   `min_x / units_per_texel` 都是整数（取整生效），但「相机动起来范围大小恒定」这一半靠的是
   包围球的旋转不变性，那是推理不是观测。
2. **掠射角的 acne**：把方向光转到几乎平行于地面，看有没有摩尔纹。当前 bias 是
   `slopeScale=2.0, constant=1`，是在光接近垂直时调的。
3. **cascade 选级**：三种 `shadow_distance`（100/15/6）下都无接缝无伪影，但场景太小，
   没能正面观测到不同 cascade 同时生效。**严格的验法是在着色端按 cascade 下标输出颜色**
   （红/绿/蓝/黄）看分层 —— 很便宜，日后怀疑选级有问题时先加这个。

**风险状态：四条全部解除**
- ✅ 深度-only framebuffer（0 个颜色附件）—— 能建。
- ✅ push constant 取整 —— 结论：shader 里不要补 padding（见 3.1）。
- ✅ `Texture2DArray` 走反射绑定路径 —— 没问题。唯一要注意 `NvrhiBindingResource` 的
  `dimension` 必须显式给。
- ✅ ZO 深度与负 viewport 高度下的 Y 方向 —— `0.5 - 0.5 * ndc.y` 一次就对。

**三个值得记住的坑**（都已修，细节在对应步骤的「结果」里）：
1. **`binding(0, 0)` 和 push constant 块撞**（反射里 push constant 占 b0）。有 push constant 的
   shader 里 cbuffer 要从 `binding(1, 0)` 开始排；纹理不受影响。
2. **shader 里不要给 push constant 补 padding** —— Slang 本来就取整到 16，补 `uint3` 反而从
   80 撑到 96。补齐是 C++ 侧的事。
3. **GPU 光源缓冲只装 `enabled` 的灯**，下标和 `RenderScene::lights` 不一样。漏了换算的表现是
   「阴影出现在另一个灯的方向上」，而只有一个灯时又恰好正确。

**顺手修掉的一个回归（重要）**：shader 的 staging 之前是 POST_BUILD，只在 target 重新链接时才跑。
改一个 `.slang` 不会导致重链，于是 `build/bin/shaders/` 里那份悄悄变旧 —— 而运行时**优先用的
正是它**，表现是「改了 shader 却没生效」。这是可搬移那个任务引入的；已改成 stamp +
`add_custom_target` + 把 shader 文件列成显式 `DEPENDS`，验过 `touch` 一个 `.slang` 会重新 Staging。

**验证手段（后面接着用）**：
`PrintWindow(hwnd, hdc, 2)`（PW_RENDERFULLCONTENT）能直接抓到播放器窗口 1280×720 的 Vulkan
渲染内容，不用把窗口弄到前台、也不会被别的窗口挡住。`Process.MainWindowHandle` 就是 SDL 窗口的
句柄（`FindWindow` 按标题找反而失败，别绕那条路）。IBL 在示例场景里很亮，`intensity 1` 的方向光
阴影很淡 —— 要看清就临时把强度调到 8，验完记得还原场景。

**决定记录**（时间倒序，新的加在最上面）：

- 2026-09-02 用户拍板：**直接做标准 CSM**，不做「单张图 v1 再升级」；shadow distance 现在就引入。
- 2026-09-02 未表态的三项由我按建议定：per-cascade 分辨率 2048² 做渲染器内常量、
  `casts_shadow` 默认 `true`、cascade 数与分割距离也先做常量。见 D6。

**踩到的坑**：（暂无）

---

## 背景与现状

### 为什么是 CSM

调研过三家主流引擎，方向光阴影**无一例外用级联**：

| 引擎 | 方案 | 默认值 |
| --- | --- | --- |
| Godot 4 | PSSM，`SHADOW_PARALLEL_4_SPLITS` **就是默认** | 分割 0.1 / 0.2 / 0.5（相对 max_distance），max_distance 100，`blend_splits` 关，`fade_start` 0.8 |
| Unreal | 传统路径就是 CSM（文档称 conventional shadow mapping 为 default）；UE5 另有 Virtual Shadow Maps 走 Nanite / 大世界 | 级联数、最大距离、质量分布、过渡位置全暴露 |
| Unity | CSM（老文档明确写 "alternatively called Parallel Split Shadow Maps"），directional 走独立 atlas | 级联数在 URP Asset 里配；URP 可选 Screen Space Shadows |

微软那份 [Common Techniques to Improve Shadow Depth Maps][msdn-common] 的原话是 CSM 是解决透视
走样「最流行的技术」，而 PSM / LSPSM / LogPSM 都试过、解决不了、还会在某些配置下退化。

Godot 保留了单张 `SHADOW_ORTHOGONAL` 模式，但文档直接标注「最快，近处会糊」—— 说明单张作为
**可选档位**合理，但没人拿它当默认。所以本任务直接做 4 级。

### 已经就位的（阴影几乎全部依赖都有）

- **`PickingPass` 是几乎现成的模板**：自带深度纹理、自己的 framebuffer、自己的小 shader、
  重画一遍几何、MVP 走 push constant。shadow pass 就是它去掉 ID 缓冲。
- `resolveDraw()`（`passes/draw_resolve.h`）已经把「校验 mesh / submesh / 材质」抽出来了，直接复用。
- `GBufferTargets` / `EnvironmentResources` 两个先例：管线拥有、具名、带 `revision()` 触发下游重建。
- 绑定是**按名字**从 Slang 反射来的（`NvrhiBindingResource::Texture("g_depth", …)`），
  加一张图 = slang 里声明一行 + C++ 里加一条。`NvrhiBindingResource` 还有 `dimension` 字段，
  所以 `Texture2DArray` 是能表达的。
- `nvrhi::FramebufferAttachment::setArraySlice()` 存在（`nvrhi.h:1286`），所以「一张 2D array
  深度图 + 每级一个 framebuffer」这个形状是现成的。
- `LightingConstants` 里 `light_count.yzw` 和 `environment_params.w` 都空着。

### 缺的

- 没有任何 shadow pass、没有 `LinearStage::Shadow`。
- `LightDesc` 没有「投不投阴影」和「阴影距离」。
- **`RenderView` 里没有 near / far**（只有 view / projection 两个矩阵）。分割视锥必须要它们。
  两个生产者（`RenderSceneExtractor`、`EditorCamera::buildRenderView`）手上都有，补两个字段即可。
- `PassPrepareContext` / `PassRecordContext` 的构造函数是**显式列举**三个共享资源的，
  加第四个要动 `linear_pass.{h,cpp}` 和 `linear_pipeline.cpp` —— 约十行胶水。

[msdn-common]: https://learn.microsoft.com/en-us/windows/win32/dxtecharts/common-techniques-to-improve-shadow-depth-maps
[msdn-csm]: https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps

---

## 设计决定

### D1 · `ShadowTargets`：一张 2D array 深度图 + 每级一个 framebuffer —— 已定

照 `GBufferTargets` 的先例另起一个具名结构，而不是塞进 `RenderTargetSet`：那个类的名字承载的是
**色彩空间**契约（scene 线性 / display 线性 / backbuffer），而阴影图里躺的是深度。

形状：`TextureDesc` 用 `Texture2DArray`、`arraySize = kCascadeCount`、`Format::D32`、
`isRenderTarget`；配 N 个 framebuffer，每个 `depthAttachment.setTexture(map).setArraySlice(i)`。
带 `revision()`，内容或尺寸变了自增，`DeferredLightingPass` 靠它重建 binding set。

**不用 atlas（一张大图切 N 块）**：array 让每级的 UV 计算完全一样（只多一个 slice 下标），
atlas 要在着色端为每级做偏移和缩放，还得防跨块采样漏到邻居。Unity 用 atlas 是因为它要把
directional / spot / point 混在一起按需分配，我们只有方向光一个消费者。

### D2 · 拟合**相机视锥**，不是场景包围盒 —— 已定，最容易被改错的一条

把视锥（每级的那一段）八个角变换到光空间，取 XY 的 min / max 当正交投影范围。

[msdn-common][msdn-common] 明确说按场景包围盒拟合会让**透视走样更严重**；它还专门提醒
「把视锥再去和场景 AABB 求交虽然更紧，但会让投影尺寸逐帧变化，**破坏 texel 取整**，所以不推荐」。

（我最初的方案写的就是「按场景包围盒拟合」，是错的。`DrawItem::world_bounds` 仍然有用，
但用在 near / far 的收紧上，见 D5，不用在 XY 范围上。）

### D3 · 分割距离：先按 Godot 的默认值 —— 已定

`0.1 / 0.2 / 0.5`（相对 `shadow_distance`），也就是四级覆盖
`[near, 0.1d] [0.1d, 0.2d] [0.2d, 0.5d] [0.5d, d]`。抄现成的默认值而不是自己推 log / uniform
混合系数：那套参数要靠看画面调，而 Godot 已经在无数项目上调过了。

### D4 · texel snapping 是必须项，不是打磨 —— 已定

正交范围的 min / max 要按 texel 大小取整（一个除、一个 floor、一个乘），并且**纹理宽高各多留
1 个 texel** 防止采样越界。

少了它的后果是**相机一动阴影边缘就闪**（shimmering）—— [msdn-common][msdn-common] 里单独一节讲
它，因为它是「看起来像实现错了」的那类瑕疵，而不是「精度不够」。这条不做完，后面调 bias 会
被闪烁干扰得没法判断。

### D5 · near / far 由光锥和场景 AABB 求交算紧 —— 已定

四个侧面由 D2 定下来之后，near / far 取「场景 AABB 在光空间里被这四个面裁剪之后的 Z 范围」。
`DrawItem::world_bounds` 的并集就是那个场景 AABB —— **当初留的接缝在这里用上**。

理由：near / far 的比值决定深度缓冲精度，卡紧同时缓解 shadow acne 和 peter-panning。
naive 做法（直接拿整个场景 AABB 的 Z）在「小视锥 + 大场景」下能差四倍。

### D6 · 哪些参数暴露到 `LightDesc`，哪些先做常量 —— 已定

**进 `LightDesc` + `DirectionalLightComponent` + 序列化 + Inspector**：

- `casts_shadow`，默认 `true`。默认投影是为了「现有场景加载后立刻能看到效果」，而不是要先去
  每个灯上打勾。反序列化时字段缺失也按 `true` 处理。
- `shadow_distance`，默认 `100.0`。拟合视锥必须要一个远端截断，否则远平面 100 米的场景阴影
  分辨率会很差。三家引擎都有这个概念（Godot `max_distance`、UE 的 CSM distance、Unity 的
  shadow distance）。

**先做渲染器内常量**（`kCascadeCount = 4`、`kShadowMapResolution = 2048`、分割 `0.1/0.2/0.5`、
`kFadeStart = 0.8`）：这几个是「质量档位」，暴露出去就要序列化 + Inspector + 取值校验，
而现在没有画质档位系统可以挂靠。等真的需要再一起提升到 `LightDesc`，那时改的只是取值来源。

### D7 · 只有第一个 `casts_shadow` 的方向光投影 —— 已定

多方向光同时投阴影意味着多套 cascade，显存和 pass 数都翻倍。v1 取第一个满足条件的方向光，
其余方向光照常照明但不投阴影。这个限制要在日志里说一次（第二个想投阴影的方向光被忽略时
warn 一条），否则场景里放两个太阳的人会以为是 bug。

### D8 · 手动 3×3 PCF，不用硬件比较采样器 —— 已定

`SamplerComparisonState` 走反射路径没验过；手动采原始深度、着色器里比较再取 3×3 平均一定能过。
省下的那点性能在这一步不值得押注。以后想上硬件 PCF 只改 shader 和一个 sampler desc。

### D9 · shadow shader 带一个空的 `fragmentMain` —— 已定

`SlangCompiler::compileGraphics` 永远要两个 entry point（默认 `vertexMain` / `fragmentMain`），
没有 vertex-only 路径。写一个 `void fragmentMain() {}` 比去改 ArtiChoco 的编译器便宜得多。

### D10 · 标准背面剔除，不做正面剔除 —— 已定

[msdn-common][msdn-common] 建议用标准背面剔除，并给了两条反对正面剔除的理由：法向不规范的
物体会直接产生瑕疵；墙根这类地方 peter-panning 和阴影缝隙**更容易**出现，因为深度差太小。
消 acne 交给 slope-scaled bias（D 项见阶段 5）。

### D11 · stage 位置：`EnvironmentBake` 之后、`Clear` 之前 —— 已定

它只写自己那张深度、不碰场景目标，和 `EnvironmentBake` 同一个性质（产物是 `Lighting` 的输入、
自己不依赖场景目标）。排在 `Clear` 之前是为了让「谁清场景目标」仍然只有 `ClearScenePass` 一处。

### 待定：无

D1～D11 全部已定。执行时发现某条行不通，**先在交接区记下来再改**，不要默默换方案。

---

## 任务清单

分七个阶段。**每个阶段结束时画面都应该是可用的**（阶段 1～3 是"画面不变"，4 之后才有阴影），
这样任何一步收工都不留半成品。

### 阶段 1 · 管线接缝（半天，不产生任何画面变化）

- [x] **1.1 新建 `ShadowTargets`**
  - 文件：`ArtiRenderer/ArtiRenderer/src/pipeline/shadow_targets.h`（新建）+ `.cpp`
  - 做法：照 `gbuffer_targets.h` 的形状写：管线拥有、`prepare(device)` 幂等、`revision()`、
    禁拷贝。持一张 `Texture2DArray` 的 D32 和 N 个 framebuffer。头部注释写清 D1 的理由
    （为什么不进 `RenderTargetSet`、为什么 array 不用 atlas）。
  - 注意：它的尺寸是**固定的**（`kShadowMapResolution`），不跟 `RenderTargetSet` 的 revision 走
    —— 阴影图分辨率和场景分辨率无关。这是它和 `GBufferTargets` 的关键区别，别照抄那边的
    `bound_revision` 逻辑。
  - 验收：编译通过。
  - 结果：`shadow_targets.{h,cpp}` 已建。幂等 prepare（建一次），不跟 `RenderTargetSet` 的 revision 走。
    常量 `kShadowCascadeCount = 4` / `kShadowMapResolution = 2048` 放在这个头里 —— 它是 array
    形状的自然拥有者，着色端也要用到级数。

- [x] **1.2 两个 context 类加第四个共享资源**
  - 文件：`pipeline/linear_pass.h`、`linear_pass.cpp`、`linear_pipeline.{h,cpp}`
  - 做法：`PassPrepareContext` / `PassRecordContext` 的构造函数各加一个 `ShadowTargets&`，
    加 `shadows()` 访问器；`LinearPipeline` 持一个成员并在构造两个 context 时传进去，
    `prepare()` 里在 `RenderTargetSet::prepare()` 之后调 `ShadowTargets::prepare()`。
  - 验收：全项目编译通过（所有 pass 都要跟着重编，但没有一个需要改）。
  - 结果：两个 context 各加了 `ShadowTargets&` 和 `shadows()`，`LinearPipeline` 持成员并在
    `m_gbuffer.prepare()` 之后调 `m_shadows.prepare()`。所有 pass 跟着重编但**一个都不用改**。

- [x] **1.3 加 `LinearStage::Shadow`**
  - 文件：`pipeline/linear_stage.h`
  - 做法：枚举里插在 `EnvironmentBake` 之后、`Clear` 之前，`linearStageName()` 补一个 case，
    并按那个文件的风格写一段注释说明为什么在这个位置（D11）。
  - 注意：stage 名会进 GPU marker 标签，所以名字定了就别改 —— capture 里看到的东西会跟着变。
  - 验收：编译通过。
  - 结果：`LinearStage::Shadow` 插在 `EnvironmentBake` 和 `Clear` 之间，`linearStageName()` 补了 case，
    并按那个文件的风格写了位置理由。

- [x] **1.4 装一个空的 `ShadowPass`**
  - 文件：`pipeline/passes/shadow_pass.{h,cpp}`（新建），`linear_pipeline.cpp` 的
    `createDeferredPipeline()` 里 `addPass(LinearStage::Shadow, …)`
  - 做法：`name()` 返回 `"Shadow"`，`isEnabled()` 先恒 `false`，`prepare` / `record` 空实现。
  - 验收：跑 `scene_editor`，装配日志里那行 stage 链出现 `Shadow`，画面和之前一致，
    Vulkan 校验层无新增报错。
  - 别忘了：`createDeferredPipeline()` 末尾那条 info 日志里的 stage 链字符串要跟着更新。
  - 结果：`shadow_pass.{h,cpp}` 空壳已装（`isEnabled()` 恒 false），`createDeferredPipeline()` 末尾
    那条日志的 stage 链已更新。实测：
    ```
    Created deferred pipeline (Bake -> Shadow -> Clear -> GBuffer -> Lighting -> Sky ->
                               Tonemap -> Debug -> Output -> UI)
    Shadow cascades created: 4 x 2048^2 (revision 1)
    ```
    画面与之前一致，无 error，校验层无新增报错（只有原有的两条第三方覆盖层警告），
    干净退出 exit 0。

### 阶段 2 · 数据补齐（一两小时，仍然没有画面变化）

- [x] **2.1 `RenderView` 加 near / far**
  - 文件：`ArtiRenderer/ArtiRenderer/include/render_scene.h`
  - 做法：加 `float near_plane{ 0.1f }; float far_plane{ 100.0f };`。注释说明它们是给阴影分割
    视锥用的 —— 光看两个矩阵推不出来（能推，但从矩阵反解 near/far 又脆又绕）。
  - 验收：编译通过。
  - 结果：`RenderView` 加了 `near_plane` / `far_plane`，注释写了为什么不从矩阵反解
    （公式对「正交/透视」和「ZO/NO 深度约定」都敏感，而生产者手上本来就有原值）。

- [x] **2.2 两个生产者填它**
  - 文件：`ArtiEngine/scene/render_scene_extractor.cpp`（从 `CameraComponent` 取）、
    `Tools/scene_editor/src/editor_camera.cpp`（`buildRenderView()`，从 `m_near_plane` /
    `m_far_plane` 取）
  - 做法：两处都填。**漏一处的后果是「编辑器里阴影对、Play 模式里不对」**，而那种 bug 两边
    单独看都像是对的。
  - 验收：两个文件都改了（这一条靠 grep 自检：`grep -rn "near_plane" ArtiEngine Tools | grep -i renderview`）。
  - 结果：两处都填了：`render_scene_extractor.cpp:48-49`（从 `CameraComponent`）和
    `editor_camera.cpp:149-150`（从 `m_near_plane` / `m_far_plane`）。两边都写了互相指认的注释，
    说明漏一处的后果是「Edit 对 / Play 不对」。

- [x] **2.3 `LightDesc` 加 `casts_shadow` / `shadow_distance`**
  - 文件：`ArtiRenderer/ArtiRenderer/include/light.h`
  - 做法：`bool casts_shadow{ true };` `float shadow_distance{ 100.0f };`。注释说明只有方向光
    读它们（点光 / 聚光的阴影是另一件事）。
  - 验收：编译通过。
  - 结果：`LightDesc` 加了 `casts_shadow{ true }` 和 `shadow_distance{ 100.0f }`，注释标明目前只有
    方向光读它们。

- [x] **2.4 组件侧打通到 Inspector**
  - 文件：`ArtiEngine/scene/components.h`（`DirectionalLightComponent` 加两个字段）、
    `scene/component_serialization.cpp`（读写，**缺字段时按默认值**）、
    `scene/render_scene_extractor.cpp`（填进 `LightDesc`）、
    `Tools/scene_editor/src/panels/inspector_panel.cpp`（一个勾选框 + 一个数值框）
  - 做法：序列化名不用改（`artiengine.directional_light` 已经在了），只是多两个键。
    反序列化必须容忍缺失 —— `projects/Assets/Scenes/1.artiscene` 是现成的老场景，它没有这两个键。
  - 验收：用编辑器打开 `1.artiscene`（不该报错）、改这两个值、存盘、重新打开，值还在；
    `git diff` 看那个 `.artiscene` 只多了两个键。
  - 结果：四处都改了：`components.h`、`component_serialization.cpp`（写无条件、读容忍缺失）、
    `render_scene_extractor.cpp`（填进 `LightDesc`）、`inspector_panel.cpp`（一个勾选框 + 一个数值框，
    后者下限 1.0 不是 0 —— 为 0 时拟合视锥的远近端重合、正交范围塔缩成一个点）。
    实测两个方向：
    - **缺失时**：原封不动的 `1.artiscene`（没有这两个键）在编辑器里加载正常、无 error。
    - **存在时**：临时手写 `CastsShadow: false` / `ShadowDistance: 42.5` 进去，`arti_player`
      加载正常、渲出 `First frame rendered (4 draw calls)`、无 error。**验完已还原** ——
      旧场景不带新键，留着当回归夹具更有用。
    写出去的那一半（存盘后文件里真的多了两个键）**没单独验**：存盘要 UI 操作，而两个
    `node[...] = ...` 是无条件的、用的也是现成 helper。阶段 4 的验收（在 Inspector 里切
    `casts_shadow` 看画面变）会自然覆盖到它。

### 阶段 3 · 画出 cascade 0 的深度图（一天）

这一阶段**代码按 N 级写、但只跑 cascade 0**：循环的上界先设成 1。这样阶段 6 铺开时不用重构。

- [x] **3.1 `shadow_depth.slang`**
  - 文件：`ArtiRenderer/ArtiRenderer/src/shaders/shadow_depth.slang`（新建）+ 登记进
    `ArtiRenderer/ArtiRenderer/CMakeLists.txt` 的 `ARTIRENDERER_SHADERS` 列表
  - 做法：`vertexMain` 把顶点变换到光空间裁剪坐标；`fragmentMain` 是空的（D9）。
    push constant 只放 `model`（64 字节），光源的 view-projection 走 UBO —— 它是逐 cascade 的，
    不是逐 draw 的，塞 push constant 会挤到 128 字节的边界上。
  - 注意：登记进 `ARTIRENDERER_SHADERS` 只影响 IDE 分组，**staging 是整目录拷的**，所以忘了
    登记不会导致运行时找不到文件（但还是要登记）。
  - 验收：编译通过；shader 能被 SlangCompiler 编出来（跑一次看日志里的
    `Compiled Slang shader '…shadow_depth.slang' to SPIR-V`）。
  - 结果：`shadow_depth.slang` 已写，登记进 `ARTIRENDERER_SHADERS`。踩了**两个坑**，两条都写进了
    shader 的注释：
    - `binding(0, 0)` 和 push constant 块撞（反射里 push constant 占的就是 b0），报
      `NVRHI: Binding layout contains duplicate bindings: b0`。改成 `binding(1, 0)` —— `gbuffer.slang`
      也是因为有 push constant 而从 1 开始排的。**纹理不受影响**（`imgui.slang` 的
      `binding(0, 0)` 是张纹理），只有 cbuffer 会撞。
    - **shader 里不要补 padding。** 我照 C++ 侧那份也在 slang 里加了 `uint3 padding`，结果块从
      80 撑到 96（cbuffer 布局下 `uint3` 不能跨 16 字节边界，从 68 起被推到 80，80+12→96），
      报 `Push constant size (80 bytes) doesn't match the size expected by the pipeline (96 bytes)`。
      Slang 本来就会把整块向上取整到 16，64+4 自然就是 80 —— 补齐是 **C++ 侧**的事。
      修完反射报 `Reflected push constants (offset 0, size 80)`，和 `ShadowConstants` 对上。

- [x] **3.2 cascade 数学（这一步是本任务的核心，单独验）**
  - 文件：`pipeline/passes/shadow_pass.cpp` 里的一个内部函数（或 `shadow_cascades.{h,cpp}`
    如果它长到该独立）
  - 做法，按顺序：
    1. 由 `view.near_plane` / `min(view.far_plane, light.shadow_distance)` 和分割比例
       算出每级的 near / far
    2. 用 `inverse(projection * view)` 把这一级视锥的八个角变换到世界空间
    3. 再变换到光空间（光的 view 矩阵：以光方向为 -Z 的任意正交基）
    4. 取 XY 的 min / max
    5. **按 texel 取整**（D4）
    6. near / far 由光锥和场景 AABB（`draws` 的 `world_bounds` 并集）求交算紧（D5）
    7. 组正交投影，**用 ZO 深度约定**（和 `glm::perspectiveRH_ZO` 一致，见风险一节）
  - 验收：这一步不产生画面。写一个临时的 `getLogChannel().debug()` 把 cascade 0 的正交范围和
    near / far 打出来，人工确认：相机往前走时范围**按 texel 跳变**而不是连续滑动
    （那就是 snapping 生效了），并且 near / far 是有限的合理值。
  - **验完把临时日志删掉。**
  - 结果：抽成了独立的 `shadow_cascades.{h,cpp}`（比预估长，放在 pass 里会把它撑得读不下去）。
    比文档多了一条关键取舍：**用包围球而不是光空间 AABB 定正交范围**。AABB 的大小随相机朗向
    变化，范围一变取整的基准也变，边缘照样闪 —— 球是旋转不变的，所以范围大小逐帧恒定，
    只有球心在动，那才是 snapping 能真的消掉 shimmering 的前提。代价是拟合更松。
    临时日志验过四级的参数（验完已删）：
    ```
    cascade 0: split=[0.10,10.09]  r=12.938  upt=0.01263  minx/upt=-1068.0000
    cascade 1: split=[10.09,20.08] r=24.188  upt=0.02362  minx/upt=-1259.0000
    cascade 2: split=[20.08,50.05] r=60.875  upt=0.05945  minx/upt=-1286.0000
    cascade 3: split=[50.05,100.00] r=120.438 upt=0.11761 minx/upt=-1326.0000
    ```
    分割距离和 0.1/0.2/0.5/1.0 对得上（near=0.1、shadow_distance=100）；
    `minx/upt` 四个都是**整数**，这就是 texel 取整生效的直接证据。

- [x] **3.3 `ShadowPass::record` 渲染 cascade 0**
  - 做法：照 `PickingPass::record` 抄骨架 —— 清深度、`ViewportState`、遍历 `scene.draws`、
    `resolveDraw()`、逐 draw 设 push constant 和顶点/索引缓冲、`drawIndexed`。
    跳过条件和 `GBufferPass` **逐条对齐**（那边不画的东西不该投阴影）。
    `isEnabled()` 改成「场景里有 `casts_shadow` 的方向光且有 draw」。
  - 验收：Vulkan 校验层无报错；GPU capture（或 `--stats`）里能看到 Shadow stage 有 draw call。
  - 结果：`ShadowPass::record` 照 `PickingPass` 的骨架写的。**比文档多做了一步**：直接渲四级，
    而不是先只渲 cascade 0。循环本来就是逐级的，先写成 1 再改成 4 只是多一个奇怔的中间
    状态；阶段 6 因此只剩着色端的 cascade 选择和淡出。
    raster 状态：标准背面剔除（D10）+ `disableDepthClip()` 开深度钳制（挡在光和物体之间、
    落在 near 之前的投影体不被剪掉）。实测：`Created the shadow depth graphics pipeline`，
    四级全部提交，**无 error、校验层无新增报错**，`First frame rendered (4 draw calls)`。

- [x] **3.4 确认深度图真的有内容**
  - 做法：最省事的验法 —— 临时在编辑器里开一个面板，用
    `ImGui::Image()` 把 cascade 0 的 slice 贴出来看。（`Renderer::sceneColorTextureId()` 那条路
    是现成的先例，照它加一个临时的 `shadowMapTextureId()`。）
  - 验收：看到一张有物体轮廓的深度图。**验完把临时面板和临时接口删掉。**
  - 如果这一步看到全白或全黑：先怀疑 ZO / Y 方向约定（见风险一节），而不是矩阵算错。
  - 结果：**这一步没按文档写的做。** 文档让临时加一个 ImGui 面板把深度图贴出来看，那要在
    `Renderer` 上开一个临时公开接口 + 改编辑器，验完再删。权衷之后改成靠 3.2 的参数日志
    推断：四级的正交盒子（±13 到 ±156）确实包住了场景，draw 全部提交且无 error，
    深度钳制也开了。
    所以严格说这一步是**推断而不是观测** —— 真正的目视确认在阶段 4。如果阶段 4 看不到
    阴影，**第一嫌疑对象仍然是 ZO / Y 方向约定，第二是“深度图本来就是空的”** ——
    后者的可能性没被完全排除，要心里有数。

### 阶段 4 · 光照里采它（一天，这一步开始有阴影）

- [x] **4.1 光照 shader 加绑定**
  - 文件：`src/shaders/deferred_lighting.slang`、`pipeline/passes/deferred_lighting_pass.cpp`
  - 做法：slang 里加 `Texture2DArray<float> shadow_map` 和 `SamplerState shadow_sampler`
    （binding 编号接在现有 10 之后）；C++ 侧在 `ensureBindingSet()` 的资源列表里加两条，
    纹理那条要设 `dimension = nvrhi::TextureDimension::Texture2DArray`。
  - 注意：binding set 的重建条件要加上 `ShadowTargets::revision()` —— 现在只看
    `gbuffer.revision()` 和 targets 的 revision。漏了它换分辨率时会绑到已销毁的纹理。
  - 验收：编译通过，跑起来不崩、画面暂时不变（还没用上）。
  - 结果：`shadow_constants`(11) / `shadow_map`(12) / `shadow_sampler`(13) 三个绑定都已反射到位：
    ```
    Reflected shader resource 'shadow_constants' at set 0, binding 11
    Reflected shader resource 'shadow_map'       at set 0, binding 12
    Reflected shader resource 'shadow_sampler'   at set 0, binding 13
    ```
    **`Texture2DArray` 这条风险解除了** —— 走反射绑定路径没有任何问题，不需要
    「N 张独立 2D 纹理」那条退路。唯一要注意的是 `NvrhiBindingResource::Texture` 默认不填
    `dimension`，对 array 纹理必须显式给 `Texture2DArray`，否则 nvrhi 会按 2D 建 SRV
    （抽了个 `shadowMapBinding()` helper）。
    binding set 的失效条件加上了 `ShadowTargets::revision()`。

- [x] **4.2 cascade 矩阵和分割距离传进去**
  - 做法：新开一个 UBO（`ShadowConstants`）装 N 个 `float4x4` 的 light view-projection、
    N 个分割距离、以及 bias / texel 大小 / `fade_start`。**不要**往 `LightingConstants` 里塞
    —— 那个结构已经 144 字节且有 `static_assert`，加 4 个矩阵会让它变成 400+ 字节，
    而且阴影参数和「这一帧怎么呈现」不是一回事。
  - `GpuLight` 也不动：投阴影的是哪个灯用 `LightingConstants` 里一个空着的 uint 槽位记下标即可
    （D7 只有一个灯投影）。
  - 验收：编译通过；用调试手段确认矩阵传对了（最简单：着色端临时把 cascade 0 的 UV 当颜色输出）。
  - 结果：新开了 `ShadowConstantsBuffer`（288 字节：4 个矩阵 + split_far + params），没往
    `LightingConstants` 里塞。
    **踩到一个文档里没预见的坑**：GPU 光源缓冲只装 `enabled` 的灯，所以它的下标和
    `RenderScene::lights` 的下标**不一样**。`ShadowTargets` 记的是后者，必须在过滤循环里
    换算成前者 —— 漏了这一步的表现是「阴影出现在另一个灯的方向上」，而只有一个灯时
    又恰好正确，非常难查。也因此把「哪个灯投影」这个结论存进了 `ShadowTargets`，
    两个 pass 不各推一次。

- [x] **4.3 手动 3×3 PCF**
  - 做法：世界坐标 → 光空间 → UV + 深度；采 3×3 个 texel 手动比较取平均（D8）。
    结果只乘到那个投阴影的方向光的直接光照项上 —— **不要乘到 IBL / 环境项**，
    那是另一回事（AO 才是环境项的遮蔽）。
  - 验收：地面上出现阴影；在 Inspector 里转动方向光，阴影跟着转；
    把 `casts_shadow` 取消勾选，阴影消失、画面回到当前的样子。
  - 结果：3×3 PCF 已实现，只作用在那个投影方向光的直接光项上（不乘 IBL）。
    UV 的 Y 用 `0.5 - 0.5 * ndc.y`，和全屏三角形那边的 `1 - uv.y * 2` 互为逆 —— **一次就对了，
    ZO / Y 方向那条风险也解除了**。
    验法：用 `PrintWindow` 直接抓播放器窗口（1280×720）做 A/B 对比。切 `CastsShadow: false`
    画面明显变亮 —— 证明采样真的生效了，也顺带排除了 3.4 那个「深度图可能是空的」
    的可能性。此时未加 bias，整片地面均匀变暗（acne 细到看不出频率，PCF 平均后就是 ~50%
    的均匀变暗）—— 预期内，阶段 5 修。

### 阶段 5 · bias 与稳定性（半天到一天，这一步决定它看起来是不是"对的"）

- [x] **5.1 slope-scaled depth bias**
  - 做法：在 shadow pass 的 raster state 上设 `depthBias` / `slopeScaledDepthBias`
    （nvrhi 的 `RasterState` 有这两个），而不是在着色端加常数偏移。
  - 验收：掠射角（光几乎平行于地面）下地面不出摩尔纹 / 条纹。
  - 注意：值要靠看画面调。**先调 slope-scaled 再调常数项** —— 前者对付的正是掠射角，
    后者调大了直接换来 peter-panning。
  - 结果：`raster_state.setSlopeScaleDepthBias(2.0f).setDepthBias(1)`。加上之后均匀变暗**完全消失**，
    地面恢复正常亮度，而且能看到清楚的头盔阴影和球体阴影。
    为了看得更清楚，临时把方向光强度调到 8（IBL 在这个场景里很亮，intensity 1 的阴影很淡）：
    地面亮白、无任何摩尔纹 / 斑点，头盔和球体各投出一块界限分明的阴影。**验完已还原场景。**
    两个数值是看画面定的，换分辨率或换级数都可能要重调。

- [x] **5.2 peter-panning 检查**
  - 验收：物体和它的阴影在接触处不脱开（墙根、箱子底边这些位置最明显）。
  - 如果 5.1 调到「不出条纹」时已经脱开了，说明 near/far 不够紧（回去看 D5）或者需要
    normal-offset（着色端沿法向偏移采样点）—— 后者是本任务范围外的下一招，别在这里硬调 bias。
  - 结果：强光那张图里头盔和球体的阴影都**贴着本体**，接触处没有脱开 —— 没有 peter-panning。
    常数项只给了 1 而不是一个大值，这是它没出现的直接原因。

- [x] **5.3 shimmering 检查**
  - 验收：**平移相机**时阴影边缘不闪。这一条专门验 D4 的 texel snapping。
  - 如果闪：确认 snapping 用的 `worldUnitsPerTexel` 是「这一级的正交范围 / 分辨率」，
    而且纹理宽高确实各多留了 1 个 texel。
  - 结果：**用户已验，没问题**（2026-09-02）。我没有交互控制相机的手段（只能启动 / 抓图 /
    关窗），所以这一条和「掠射角 acne」「 cascade 选级」一起交给用户在编辑器里看了。
    我这边的间接证据是 3.2 里 `min_x / units_per_texel` 四级都是整数。

### 阶段 6 · 铺开到 4 级（一天）

- [x] **6.1 渲染四级**
  - 做法：3.2 的循环上界从 1 改成 `kCascadeCount`，每级用自己的 framebuffer（array slice）。
  - 验收：GPU capture 里 Shadow stage 有四组 draw；深度图四个 slice 都有内容。
  - 结果：阶段 3 就做完了（直接渲四级，没走「先只渲 cascade 0」那个中间状态）。

- [x] **6.2 着色端选 cascade**
  - 做法：按片元的 **view-space 深度**和分割距离比，选第一个覆盖它的 cascade。
  - 验收：近处阴影明显比阶段 5 清晰；**能看出 cascade 之间的接缝**——
    这是预期结果，混合在 7.1。
  - 注意：view-space 深度要从已经反投影出来的世界坐标算（`length(camera_position - world_pos)`
    还是 `-(view * world).z`？后者才是 cascade 分割用的那个量，前者是径向距离，
    用错会让屏幕边缘提前跳级）。
  - 结果：选级用的是 `dot(world_pos - camera_pos, camera_forward)` —— 等价于 `-(view * world).z`，
    也正是 `computeShadowCascades` 里算分割距离用的那个量。`camera_forward` 新加到 `ShadowConstants`
    里传过去（从逆 view-projection 反解前向又绕又脓），且与 cascade 数学里的取法保持一致。
    **验得不够硬，说清楚：** 试了三种 `shadow_distance`（100 / 15 / 6），三次都无 error、
    无级间接缝、无伪影；但因为这个场景尺度小，没能**正面观测到不同 cascade 同时生效**。
    严格的验法是在着色端按 cascade 下标输出颜色（红/绿/蓝/黄）看分层 —— 很便宜，日后怀疑
    选级有问题时先加这个。

- [x] **6.3 `shadow_distance` 和远端淡出**
  - 做法：超过 `shadow_distance` 的片元不采阴影；在 `kFadeStart`（0.8）到 1.0 之间线性淡出，
    否则边界上会有一条硬边。
  - 验收：把 `shadow_distance` 调小，能看到阴影在指定距离外平滑消失而不是硬切。
  - 结果：超过 `shadow_distance` 直接返回无阴影；`fade_start`（0.8）到 1.0 之间线性抹平。
    实测三个距离的单调行为符合预期：100 和 15 时头盔和球体的阴影都在；降到 6 时它们淡出了
    （地面那一块已超过 6 个单位），而且**没有硬边** —— 亮地面上一条硬边会非常显眼，
    没看到就是淡出生效了。**验完已还原场景。**

### 阶段 7 · 收尾

- [ ] **7.1（可选）cascade 之间混合**
  - 做法：在相邻 cascade 的重叠带里采两级取插值。Godot 的 `blend_splits` 默认是**关**的，
    所以这一条可以先不做 —— 但要在文档里写明接缝是已知的。
  - 验收：做了的话，接缝消失；没做的话，7.2 里记下来。

- [x] **7.2 文档**
  - 文件：`docs/Architecture/Rendering.md`（stage 顺序表加 Shadow 一行、跨 pass 资源表加
    `ShadowTargets`、着色器列表加 `shadow_depth.slang`）、
    `docs/Architecture/README.md`（「明确未做」里删掉「阴影」那条，改成剩下的：
    点光 / 聚光阴影、cascade 混合、软阴影）、
    `docs/Architecture/Scene.md`（`DirectionalLightComponent` 的字段表加两个字段）
  - 验收：`grep -rn "没有 shadow pass\|管线里没有 shadow" docs/` 没有过期残留。
  - 结果：`Rendering.md`：stage 表加了 `Shadow` 一行、`Lighting` 那行补了「也是唯一采阴影图的地方」、
    跪 pass 资源表加了 `ShadowTargets`、着色器列表加了 `shadow_depth.slang`。
    `Scene.md`：`DirectionalLightComponent` 的字段表加了两个新字段。
    `README.md`：「阴影」那条缺口拆成了两条 —— 「阴影的剩余部分」（点光 / 聚光 / 级间混合 / 软阴影）
    和「阴影的视锥剔除」（四级意味着几何一帧画五遁）。

- [x] **7.3 提交**
  - 主要改动在 ArtiRenderer；ArtiEngine 侧是组件 / 序列化 / 抽取 / Inspector。
    ArtiChoco 这次**应该不用动** —— 如果动了，说明有个设计决定变了，先记进交接区。
  - 验收：`git status` 三个仓库都干净；submodule 指针按内 → 外顺序提交。
  - 结果：本地提交完成（ArtiRenderer + ArtiEngine 两层，ArtiChoco 未动 —— 和预期一致）。
    `ctest` 过。**未 push**，等用户点头；推的顺序仍然是内 → 外。

---

## 端到端验收

全部做完之后，在编辑器里按顺序过一遍：

1. 打开 `projects/projects.artiproj`，加载 `Assets/Scenes/1.artiscene`（老场景，没有新字段）
   —— 不该报错，方向光默认投阴影。
2. 摆一个立方体在地面上方：地面上出现它的阴影，接触处不脱开。
3. **转动方向光**：阴影跟着转，掠射角下不出条纹。
4. **平移相机**：阴影边缘不闪。
5. **拉远相机**：近处阴影清晰，远处变糊但不闪；能看出 cascade 接缝（如果没做 7.1）。
6. 把 `shadow_distance` 从 100 调到 20：阴影在 20 米外平滑消失。
7. 取消 `casts_shadow`：阴影完全消失，画面回到做这个任务之前的样子。
8. 进 Play 模式：阴影表现和 Edit 模式一致（这一条验的是 2.2 两处都填了）。
9. `arti_player` 跑同一个项目：同样有阴影。
10. `ctest` 过。

---

## 风险与注意

### 深度约定（最容易错一次的地方）

本项目用 `glm::perspectiveRH_ZO`（深度范围 [0,1]，**不是** reverse-Z），而 NVRHI 的 Vulkan 后端
用**负 viewport 高度**、走 D3D 式屏幕约定 —— 所以业务代码里**不做** `projection[1][1]` 翻转
（见 `docs/Architecture/Rendering.md` 和 ArtiChoco 的 renderer README）。

阴影这边要保证两件事一致：
1. 正交投影也用 ZO 约定（`glm::orthoRH_ZO`），否则深度比较的方向是反的 —— 表现是整张图全亮或全暗。
2. **渲深度图时的 Y 方向和采样时算 UV 的 Y 方向必须一致。** 负 viewport 高度对 shadow pass
   同样生效，所以从光空间 NDC 换算 UV 时那个 `1 - y` 到底要不要，取决于实测。

阶段 3.4 看到全白 / 全黑时，先怀疑这两条，而不是矩阵算错。

### 深度-only framebuffer 能不能建

`FramebufferDesc::colorAttachments` 是个 `static_vector`，空的是能表达的，但 NVRHI 的 Vulkan
后端接不接受「0 个颜色附件 + 1 个深度附件」没验过。**阶段 1.1 建 framebuffer 的时候就会撞。**

撞不过的退路：挂一张 `R8_UNORM` 的假颜色附件（和阴影图同尺寸，浪费 4 MB）。要走这条就在
交接区记一条，别默默加。

### `Texture2DArray` 走反射绑定路径没验过

现有的十一个绑定全是 `Texture2D` / `TextureCube` / `StructuredBuffer` / `SamplerState`。
`NvrhiBindingResource` 有 `dimension` 字段，看起来是为这种情况留的，但 Slang 把
`Texture2DArray<float>` 反射成什么、`createNvrhiBindingSet` 会不会正确处理，要**阶段 4.1 实测**。

退路：改成 N 张独立的 `Texture2D` + N 个 binding（丑，而且 cascade 数写死在 shader 里，
但一定能过）。同样，走这条要记进交接区 —— 它会让 7.1 的混合和以后加 cascade 数变难。

### push constant 预算

`model` 是 64 字节。Slang 会把 push constant 块大小向上取整到最大成员的对齐（`float4x4` → 16），
所以实际是 64 或 80，取决于有没有别的成员。`PickingConstants` 上有过一次
「68 被取整成 80、不补齐就被 NVRHI 按大小拒掉」的教训，那个文件的注释里写着，照它做。

Vulkan 保证的 push constant 下限是 128 字节 —— 只放 `model` 有充足余量，**不要**顺手把
cascade 的矩阵也塞进去。

### 性能：几何多画四遍

四级 cascade 意味着场景几何一帧画五遍（G-Buffer + 4 级阴影）。当前没有视锥剔除
（`FrameStatistics::culled` 恒为 0），所以每级都会把**整个场景**画一遍，包括那一级正交范围外的
物体。小场景无所谓，但这是「视锥剔除」这个任务第一次有明确收益的地方 —— 剔除应该按每级的
光锥做，不是按相机视锥。做完这个任务后可以把那条缺口的优先级往上提一提。

### 不在本任务范围内

- 点光 cubemap 阴影、聚光阴影
- 软阴影（PCSS / VSM / 硬件比较采样器）
- 多个方向光同时投阴影（D7）
- cascade 混合（7.1 可选）
- 阴影相关参数的画质档位系统（D6）
- 阴影的视锥剔除（见上）

---

## 参考

- [Common Techniques to Improve Shadow Depth Maps][msdn-common] —— acne / peter-panning /
  透视走样 / shimmering 的规范解法。D2 / D4 / D5 / D10 都出自这里
- [Cascaded Shadow Maps][msdn-csm] —— CSM 的具体算法和 `ComputeNearAndFar` 的做法
- [Godot 4.6 DirectionalLight3D](https://docs.godotengine.org/en/4.6/classes/class_directionallight3d.html)
  —— 默认值抄的是这里（4 splits、0.1/0.2/0.5、max_distance 100、fade_start 0.8）
- [Shadowing in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/shadowing-in-unreal-engine)
  —— CSM 是传统路径的默认，VSM 是 UE5 的新路径
- [Shadows in URP](https://docs.unity3d.com/Packages/com.unity.render-pipelines.universal@16.0/manual/Shadows-in-URP.html)
  —— atlas 分配和屏幕空间阴影（我们不走这两条，见 D1）
- [A Sampling of Shadow Techniques](https://therealmjp.github.io/posts/shadow-maps/)、
  [Shadow Maps (part 1) – The Witness](http://the-witness.net/news/2010/03/graphics-tech-shadow-maps-part-1/)
  —— 实践者视角的取舍。这两篇只看了摘要，没逐条核
