# ArtiEngine 架构

> 面向「刚接手这个仓库」的读者：先读本页拿到全局，再按需跳进分册。
> 每一册只讲 **ArtiEngine 这一层的决定**。基础框架（ArtiChoco）的内部细节在它自己的
> README 里，本目录只引用、不复制。

## 1. 这是什么

一个 Vulkan / NVRHI 的 3D 引擎，最终交付三个可执行文件：

| 可执行 | 位置 | 作用 |
| --- | --- | --- |
| `scene_editor` | `Tools/scene_editor` | 编辑器：开项目、编场景、导资产、Play 预览 |
| `arti_player` | `Runtime/player` | 独立播放器：加载 `.artiproj` + 场景并跑起来 |
| `asset_tools` | `Tools/asset_tools` | 无窗口 CLI：对账、导入、改设置、提取材质、打包 |

三者共享同一份运行时（`ArtiEngine::Runtime`）。这是整个分层里最硬的一条约束：
**编辑器 Play 模式和 exe 跑出来的必须是同一份代码**，否则两边分开看都对、效果却各自漂移，
那种 bug 最难查。

## 2. 分层

```
┌── 应用层 ─────────────────────────────┬──────────────────┐
│  Tools/                               │  Runtime/        │
│    scene_editor  asset_tools          │    arti_player   │
├───────────────────────────────────────┴──────────────────┤
│  ArtiEngine/          引擎层（本仓库自己写的）             │
│    Engine  资产类型 / importer / loader / 组件 / 抽取     │
│    Runtime World / AssetRuntime / SceneRenderer          │
│    ImGui   ImGuiHost（SDL3 后端 + 字体 + docking）        │
├──────────────────────────────────────────────────────────┤
│  ArtiRenderer::Renderer     场景渲染器（submodule）        │
│    RenderScene → 延迟管线 → 屏幕 / 离屏纹理               │
├──────────────────────────────────────────────────────────┤
│  ArtiChoco                  基础框架（嵌套 submodule）     │
│    Core  Platform  Renderer  Scene  Asset  Project       │
├──────────────────────────────────────────────────────────┤
│  ArtiSDK   Vulkan · GLM · SDL3 · Slang（都从 Vulkan SDK 取）│
│  第三方    NVRHI · EnTT · spdlog · enkiTS · yaml-cpp      │
│            ImGui · ImGuizmo · cgltf · stb_image · Box3D  │
└──────────────────────────────────────────────────────────┘
```

各层的职责边界：

| 层 | 只负责 | 刻意不知道 |
| --- | --- | --- |
| `ArtiChoco::Core` | Application / Layer / Window 抽象 / 事件 / 输入 / 日志 / UUID / 任务系统 / Timestep | 渲染、场景、资产 |
| `ArtiChoco::Platform` | SDL3 窗口和 Vulkan surface source | 引擎语义 |
| `ArtiChoco::Renderer` | `RenderDevice`：Vulkan bootstrap、NVRHI 设备、swapchain、帧管理、Slang 编译与反射、buffer / texture 资源 | 场景、材质、光照 |
| `ArtiChoco::Scene` | EnTT 之上的 ECS：实体、层级、世界变换、系统 stage、克隆、YAML 序列化 | 具体有哪些游戏组件 |
| `ArtiChoco::Asset` | 资产框架：storage / catalog / importer 接口 / loader 接口 / 三方对账 / 设置三层解析 | 具体有哪些资产类型 |
| `ArtiChoco::Project` | `.artiproj` 的读写与 `Assets` / `Library/Artifacts` 两个根 | 其他一切 |
| `ArtiRenderer::Renderer` | 从 `RenderScene`（网格 + 材质 + 灯光 + 环境）到画面：延迟管线、IBL、tone mapping、GPU 拾取、调试线 | 场景怎么组织、资产怎么来的 |
| `ArtiEngine::Engine` | 引擎的**数据模型**：四种资产类型、三个 importer、四个 loader、八个游戏组件、`Scene → RenderScene` 抽取 | 主循环、窗口 |
| `ArtiEngine::Runtime` | **驱动**数据模型的循环：`World` 的 tick、刚体物理（Box3D）、资产工作区、一帧的 prepare / submit | UI、编辑器状态 |
| `ArtiEngine::ImGui` | ImGui 上下文、SDL3 事件桥、字体、docking，产出 `FrameOverlay` | 画什么面板 |

## 3. target 依赖

链接关系就是分层的可机检形式 —— 依赖方向写反了在配置期或链接期就会暴露。下表是每个 target
**直接**链的东西（`ArtiSDK::GLM` 几乎无处不在，省略）：

| target | 类型 | 直接依赖 |
| --- | --- | --- |
| `ArtiChoco::Core` | 静态库 | spdlog · enkiTS |
| `ArtiChoco::Renderer` | 静态库 | Core · `ArtiSDK::Vulkan` · nvrhi · nvrhi_vk(P) · `ArtiSDK::Slang`(P) |
| `ArtiChoco::Platform` | 静态库 | Core · Renderer · `ArtiSDK::SDL3`(P) |
| `ArtiChoco::Scene` | 静态库 | Core · EnTT · yaml-cpp |
| `ArtiChoco::Asset` | 静态库 | Core · yaml-cpp(P) |
| `ArtiChoco::Project` | 静态库 | Core · yaml-cpp |
| `ArtiRenderer::Renderer` | 静态库 | ArtiChoco::{Core, Renderer} · `ArtiSDK::Slang`(P) |
| `ArtiEngine::Engine` | 静态库 | `ArtiRenderer::Renderer` · ArtiChoco::{Core, Scene, Asset} · cgltf(P) · stb(P) |
| `ArtiEngine::Runtime` | 静态库 | `ArtiEngine::Engine` · box3d(P) |
| `ArtiEngine::ImGui` | 静态库 | `ArtiEngine::Engine` · `ImGui::Core` · ArtiChoco::Platform(P) · SDL3(P) · `ImGui::SDL3Backend`(P) |
| `ArtiTools::Asset` | 静态库 | `ArtiEngine::Runtime` · `ArtiChoco::Project` |
| `ArtiTools::Platform` | 静态库 | ole32 · shell32（仅 Windows） |
| `asset_tools` | 可执行 | `ArtiTools::Asset` · ArtiChoco::{Core, Project} |
| `arti_player` | 可执行 | `ArtiEngine::Runtime` · `ArtiEngine::ImGui` · ArtiChoco::{Core, Platform, Project} |
| `scene_editor` | 可执行 | `ArtiEngine::{Engine, Runtime, ImGui}` · `ArtiTools::{Asset, Platform}` · ArtiChoco::{Core, Platform, Project} · ImGuizmo |

`(P)` = PRIVATE，即不出现在公开头里、下游不继承。

三个值得知道的取舍：

- **`Engine` 和 `Runtime` 分成两个库**。`Engine` 是数据模型，`Runtime` 是驱动它的循环 ——
  这条界限已经兑现过一次：物理（box3d）进的是 `Runtime`，而且是 PRIVATE，所以只消费数据类型的
  目标看不见 box3d 的头。将来的脚本 VM 同理。**代价**：`ArtiTools::Asset` 链 `Runtime`，所以
  `asset_tools` 会跟着链上 box3d 但永远不建物理世界 —— 和它链了 Vulkan 却从不建 `RenderDevice`
  是同一种情况。真嫌重的话，出路是把 `AssetPipeline` 从 `Runtime` 上摘下来。
- **ImGui 宿主单独一个库**。它要链 SDL3 和 imgui 的 SDL3 后端；核心库的下游不该因为
  「可能要画 UI」就背上窗口系统。`ArtiChoco::Renderer` 和 `ArtiRenderer::Renderer` 都不链
  Platform，这条界限从下到上是一致的。
- **`Engine` / `Runtime` 都不依赖 Platform 或 SDL**。这正是 `asset_tools` 能作为无窗口 CLI
  存在的原因：它链进了渲染类型（`MeshAsset` 里有 `rendering::MeshVertex`），但从不创建
  `RenderDevice`。相应地 `GPUAssetCache` 刻意留在 `AssetRuntime` 之外 —— 谁有 Renderer 谁持
  GPU 缓存。

`ArtiTools::Platform` 目前只有 Windows 的原生文件对话框实现，其他平台在 CMake 配置期直接
`FATAL_ERROR`（而不是给个返回空路径的桩：桩会让编辑器正常起来但所有对话框静默失灵，
比配置期报错难查得多）。

## 4. 三条主干

### 资产：源文件 → 画面

```
Assets/foo.gltf ─importer─→ Library/Artifacts/<uuid>.artimesh ─loader─→ MeshAsset
                                                                          │
                                                          GPUAssetCache ──┴─→ MeshHandle
```

`Assets/` 的源文件加 `.meta` 是唯一真相，`Library/` 完全可推导、可随时整个删掉。
详见 [Assets.md](Assets.md)。

### 场景：磁盘 → ECS → 渲染数据

```
.artiscene ──SceneSerializer──→ Scene（EnTT）──RenderSceneExtractor──→ RenderScene
```

`RenderScene` 是引擎和渲染器之间**唯一**的契约：一个视图、一串 `DrawItem`、一串 `LightDesc`、
一份环境。详见 [Scene.md](Scene.md)。

### 一帧（以编辑器为例）

`Application::run()` 每帧按 `onUpdate` → `onImGuiRender` → `onRender` 遍历 layer 栈：

```
onUpdate       Simulate / Play → World::tick(dt)（FixedUpdate 补齐，然后 Update / LateUpdate）
               Edit / Simulate → 更新编辑器相机（两条轴各判一次，见 Applications.md）
onImGuiRender  生成 UI draw data（不提交）：菜单、工具条、面板、Viewport、gizmo
               Viewport 里的点击 → Renderer::requestPick()
onRender       SceneRenderer::prepare()   抽取这一帧的 RenderScene
               ↓ 这个窗口里提交调试线（选中轮廓、光源线框）—— 要用这一帧的相机
               SceneRenderer::submit()    renderFrame(scene, imgui overlay)
               Renderer::takePickResult() → 更新选中实体（读回异步，隔几帧才有结果）
```

`submit()` 是**唯一**提交 ImGui draw data 的地方，所以 `onRender` 必须无条件走到它 ——
任何 early return 都是整个界面黑屏，而黑屏时用户连菜单都点不到、没法自救。「这一帧没有可画
的场景」（项目没开、面板尺寸为 0、没有相机）统一处理成提交一个空场景：没有 draw，只有
overlay。详见 [Rendering.md](Rendering.md)。

## 5. 目录约定

### 仓库

```
ArtiEngine/          引擎层源码
  asset/               资产类型、importer、loader、GPU 缓存、builtin
  scene/               游戏组件、序列化、RenderScene 抽取
  runtime/             World / AssetRuntime / SceneRenderer
  imgui/               ImGuiHost
Runtime/player/      独立播放器
Tools/               编辑器、CLI、工具专用平台代码、UI 资源（字体）
ArtiRenderer/        submodule（内含嵌套 submodule ArtiChoco 和 imgui）
third_party/         ImGuizmo、box3d（都是 submodule）、cgltf
projects/            示例项目（既是手动验证场地，也是磁盘布局的活文档）
docs/Architecture/   本目录
```

### 项目（开发中）

```
<Root>/
  <Name>.artiproj              项目文件（YAML）：名字、起始场景、两个根路径
  Assets/                      源文件 + 每个源文件一份 .meta ← 唯一真相，进版本控制
    Model/DamagedHelmet/DamagedHelmet.gltf(+.meta)
    Scenes/1.artiscene
    Skybox/newport_loft.hdr(+.meta)
  Library/Artifacts/           完全可推导，可随时删
    Imported/<uuid>.arti{mesh,texture,material,prefab}
    Builtin/{Cube,Sphere}.mesh · Default.material
```

`.meta` 必须跟源文件一起进版本控制、一起改名 —— 资产身份（UUID）存在里面，删掉它会让场景
引用静默断开。

### 项目（打包后）

`asset_tools pack` 产出一个**自足**的目录：整个拷到别的机器上双击 `arti_player.exe` 就能跑，
不需要装 Vulkan SDK，也不需要源码树在原位。

```
<Out>/
  <Name>.artiproj              LastOpenScene 已清掉
  catalog.artimanifest         catalog 快照，运行时靠它建 catalog（不再扫 .meta）
  Library/Artifacts/**         所有 artifact，含 builtin
  Assets/**/*.artiscene        只有场景，没有源模型 / 贴图 / .meta
  shaders/**                   内建 .slang —— 着色器是运行期编译的，必须跟着走
  *.dll                        SDL3 / slang / slang-compiler + MSVC CRT redist（10 个）
  arti_player.exe              播放器本体（`--no-player` 可以不带）
```

后三项来自 `asset_tools` **自己 exe 旁边**那一份（构建时 staging 放过去的），所以打包不需要
知道 SDK 和源码树在哪，而且自动跟着构建配置走（Debug 拿 `SDL3d.dll`，Release 拿 `SDL3.dll`）。

## 6. 构建

```powershell
git submodule update --init --recursive   # 嵌套两层，少一层在配置期直接报错
cmake --preset debug                      # Ninja + clang，compile_commands.json 落在 build/
cmake --build --preset debug
```

- **Vulkan SDK 是硬依赖**。Vulkan / GLM / SDL3 / Slang 全从它取（`ArtiVulkanSDK.cmake`），
  找不到就在配置期 `FATAL_ERROR`。设环境变量 `VULKAN_SDK` 或传 `-DARTI_VULKAN_SDK=<path>`。
- 着色器（`.slang`）和编辑器字体都是**两段查找**：先看 exe 旁边（`shaders/` / `resources/`，
  构建时 POST_BUILD 拷过去的），没有才回落到构建期注入的源码树绝对路径
  （`ARTIRENDERER_SHADER_DIR` / `ARTIENGINE_TOOLS_RES_DIR`）。所以产物可搬移，同时开发期改
  shader 或换字体仍然不用重编 C++。shader 是**整目录二选一**，不逐文件回落 —— `.slang` 之间
  有 `#include`，混用两个根会报出跟路径无关的编译错误。选中哪个根会记一条 info 日志。
- 运行时 DLL 由 `artichoco_stage_vulkan_sdk_runtime()` 拷到 exe 旁边。注意 `slang.dll` 只是个
  转发器，实现在 `slang-compiler.dll` 里、由它在运行时 `LoadLibrary` 加载 ——
  `$<TARGET_RUNTIME_DLLS>` 看不见这条依赖，所以那个函数里显式补了一条。
- MSVC 的 CRT 可再分发 DLL 由 `artichoco_stage_msvc_runtime()` 拷到 exe 旁边（整个
  `Microsoft.VC<n>.CRT` 目录，10 个文件）。产物用动态 CRT（`/MD`），而 `msvcp140` /
  `vcruntime140` 那几个不是 Windows 自带的 —— `api-ms-win-crt-*` 那个 UCRT 才是。目录整拷而不是
  只挑导入表里那三个：`VCRUNTIME140_1.dll` 是 `MSVCP140.dll` 自己拉进来的，不在 exe 的静态
  导入表里。CRT 找不到时只警告不报错 —— 那只影响产物能不能拿去别的机器，不影响本机构建。
  CMake 自带的 `InstallRequiredSystemLibraries` 在这条工具链上不可用（它 `if(MSVC)` 把门，
  而独立 clang 走 MSVC ABI 时 `MSVC` 是空的），所以发现逻辑自己写在 `ArtiMsvcRuntime.cmake`。
- `ARTIENGINE_BUILD_TOOLS=OFF` 可以只建引擎和播放器。`Runtime/` 没有开关 —— 运行时是交付物，
  不是可选工具。
- 测试：`ctest`。目前 ArtiEngine 侧只有 `asset_pipeline_smoke`，渲染侧的冒烟覆盖在
  ArtiRenderer / ArtiChoco 自己的 example 和 test 里。

代码风格约定：4 空格、100 列、LF。仓库根有 `.clang-format`，但**既有文件并没有按它格式化
过** —— 改老文件时手写成周围的风格，不要跑格式化工具，否则 diff 里全是与改动无关的噪声。

### 运行要求（打包产物）

产物自带 CRT、SDL3、slang 和着色器，所以目标机器只需要这三样：

| 要求 | 说明 |
| --- | --- |
| Windows 10+ x64 | UCRT（`api-ms-win-crt-*` / `ucrtbase.dll`）是系统自带的；更老的 Windows 要额外装 UCRT 更新 |
| 支持 **Vulkan 1.3** 的显卡驱动 | `deviceScore()` 里 `apiVersion < VK_API_VERSION_1_3` 直接返回 0，达不到就抛 `No Vulkan device supports graphics, presentation, and swapchains`。Vulkan 1.3 是 2022 年初发布的，所以实际门槛是驱动别太旧 |
| （不再是硬要求）产物目录可写 | `logs/ArtiChoco.log` 建在 exe 旁边，但**建不出来只警告、不再阻止启动**。只读安装（`C:\Program Files\...`）能跑，只是日志只在控制台 |

`vulkan-1.dll` 由显卡驱动提供，刻意不随产物走。

**不支持旧设备是明确的产品决定**，所以不要为 Vulkan 1.2 或更早的设备加回落路径。
`supportsRenderingExtensions()` 里那条「1.1 / 1.2 设备只要有 `VK_KHR_dynamic_rendering` +
`VK_KHR_synchronization2` 也算支持」的分支因此是**死代码** —— `deviceScore()` 的 1.3 门槛在它
之前就把设备刷掉了。两处对「最低要求」的判断不一致，但结论一致，删掉那条分支只是让代码说出
本来的意思，不改变行为。

## 7. 明确未做

写下来是为了让「没有」和「找不到」区分开。

| 空缺 | 现状与接缝 |
| --- | --- |
| 视锥剔除 | `FrameStatistics::culled` 恒为 0。接缝已留：抽取时算好 `DrawItem::world_bounds`，`Renderer::meshInfo()` 能拿到局部包围盒（顶点数据已经不在 CPU 侧） |
| 阴影的剩余部分 | 方向光的级联阴影已经有了（4 级、拟合视锥、texel snapping、3×3 PCF、slope-scaled bias、shadow distance + 淡出）。还没有的：点光 cubemap 阴影、聚光阴影、cascade 之间的过渡混合（Godot 的 `blend_splits` 默认也是关的，所以级间能看出接缝）、软阴影（PCSS / VSM / 硬件比较采样器） |
| 阴影的视锥剔除 | 四级 cascade 意味着几何一帧画五遍（G-Buffer + 4 级），而没有剔除时**每级都把整个场景画一遍**，包括那一级正交范围外的物体。剔除要按每级的光锥做，不是按相机视锥 |
| 源内容变更检测 | `.meta` 的 `ContentHash` / `Size` / `Importer.Version` **只写不读**。目前只有 artifact 缺失才触发重导，改了源文件内容必须手动重导 |
| 多线程的消费者 | **任务系统本身已经有了**（`arti::core::TaskSystem`，enkiTS 封装：fork-join、带句柄的异步任务、钉线程任务、`TaskGraph` 依赖图，文档见 `core/task/README.md`），但**一个真实消费者都还没接**。每个接入点的位置和它该调的 API 列在下面那张表里 |
| 脚本 / 音频 | 完全没有。`Update` / `LateUpdate` 两个 stage 空着在跑，是它们现成的挂载点（`FixedUpdate` 现在被物理占了） |
| 物理的其余部分 | 刚体已经有了（球 / 盒 / 胶囊、静态 / 运动学 / 动态、休眠、堆叠）。还没有的：射线查询、触发器（sensor）、关节、三角网格 / 高度场碰撞体、复合体（一个 body 多个 collider）、角色控制器、多线程（桥到任务系统，见 7.1）。Box3D 这些都有，随时能加 —— 但现在没有脚本，查询结果没人消费 |
| 带父级或有缩放的实体不参与模拟 | 物理在世界空间算而 `TransformComponent` 是局部的，带父级要拿父级的世界逆矩阵反算；碰撞体尺寸显式写在组件里、不跟 scale 走。两种情况都记一条 warn 并跳过，所以「一块大地面」要拆成缩放过的视觉体 + 不缩放的碰撞体两个实体 |
| 渲染插值 | 物理按固定步长跑，画面按帧率画，所以快速运动会有细微抖动。接缝是现成的：`FixedTimestepAccumulator::alpha()`（当前余额占一个固定步的比例）还没人用 |
| 前向管线 | 已整条移除，只有延迟一条路径。不留双路径是刻意的 |
| 场景作为资产 | `.artiscene` 没有 handle、没有 artifact，靠项目根相对路径引用 |
| rename / delete 感知 | 在文件管理器里改名会让旧 UUID 变孤儿被回收、新文件拿新 UUID，场景引用静默失效。变通办法是连 `.meta` 一起改名 |
| 非 Windows 平台 | 两处卡住：`ArtiTools::Platform`（文件对话框）只有 Win32 实现；运行时依赖 staging 在非 Windows 上只设 `BUILD_RPATH`、不拷文件，所以产物可搬移性只在 Windows 上验过 |
| 真实干净机器的验收 | Debug / Release 的可搬移性都验过（clean PATH + 回落路径失效下能渲染），CRT 也确认加载的是产物里那一份。但没在一台真正没装 VC++ Redistributable / 没装 Vulkan 驱动的机器上跑过 |
| 只读位置不留日志文件 | `logs/ArtiChoco.log` 固定建在 exe 旁边。建不出来**不再致命**（降级成只输出控制台并警告一次），但装在只读位置时就没有日志文件可查。要让那种安装也留日志，得改成写 `%LOCALAPPDATA%` —— 那会挑走开发期习惯的位置，是另一个决定 |
| 播放器是 console 子系统 | 双击会多开一个黑框；失败信息只在那个控制台和日志里，没有弹窗，所以驱动不达标的人看到的是一闪而过的黑框 |
| 发布只能用 Release | Debug 产物链的调试 CRT（`ucrtbased.dll` 等）不可再分发，而 staging 拷的是 redist 里的 release CRT。目前没有机制防止用 Debug 构建去 pack |
| 导入设置 / Extract 的编辑器 UI | `AssetPipeline` 的 `sourceSettings` / `setAuthoredSetting` / `extractMaterial` 都在，但只有 CLI 在调。Content Browser 的预览栏目前只显示状态，不能改设置 |
| 打包的编辑器入口 | 没有「Build」菜单项，只有 `asset_tools pack` |

### 7.1 任务系统还没接的那些消费者

任务系统这一层是**刻意单独做完、不带消费者**的：先把线程基座和 API 定下来，再一个一个接，
每次接一个都是独立的、可单独验收的改动。下面每一行都能指出现在的位置和它会调的 API ——
写不出 API 的行会是设计漏洞，所以这张表也是那一层的自检。

| 未来消费者 | 现在在哪 | 会调什么 |
| --- | --- | --- |
| 资产 reconcile 的 scan | `AssetPipeline::planReconcile()` / `scan()` | `parallelFor(count, fn, {min_range})`。`scan()` 是纯读、无共享写，换过去语义不变 |
| 资产导入的拓扑序 | `AssetPipeline::reconcile()` | `TaskGraph`：一个源文件一个节点，依赖边就是拓扑序 |
| 纹理 / 网格解码 + 上传 | `GPUAssetCache` | `TaskGraph`：解码节点在 worker 上，上传节点 `addPinnedAfter` 钉在渲染线程。Rendering.md 第 1 节已经把这个界限划好了 |
| 视锥剔除 / 抽取 | `RenderSceneExtractor::extract()` | `parallelForRanges` + `threadIndex()` 做每线程 bucket（`DrawItem::world_bounds` 每帧已经算好） |
| 物理多线程 | `PhysicsSystem` → Box3D 的 `b3EnqueueTaskCallback` | `submitParallelFor` 拿句柄 + `wait(handle)` —— Box3D 的任务回调要的正是这个形状 |
| 渲染线程 | 三个 exe 的 layer | `TaskSystemConfig::external_thread_count` + `registerExternalThread()` + 长驻 `submitPinned`（`examples/test_app/render_system.cpp` 已经演示过形状） |

## 8. 分册

| 文档 | 内容 |
| --- | --- |
| [Assets.md](Assets.md) | 四种资产类型、artifact 格式、importer / loader、`AssetRuntime` 与 `AssetPipeline` 的分工、GPU 缓存、builtin |
| [Scene.md](Scene.md) | ECS 基座、六个游戏组件、序列化名、`World` 的 tick、抽取成 `RenderScene` |
| [Rendering.md](Rendering.md) | 渲染三层、`RenderScene` 契约、延迟管线的 stage 顺序与理由、两种呈现模式、拾取、调试线 |
| [Applications.md](Applications.md) | 编辑器结构与 Play 模式、独立播放器、CLI 与打包发布 |

上游文档（在 submodule 里，讲框架本身）：

- `ArtiRenderer/ArtiChoco/artichoco/asset/README.md` —— 资产框架完整设计（reconcile、sidecar
  v2、设置三层解析、prescan / 推断 / 拓扑序）。注意它标着「草稿」，且写在移除 OBJ / MTL
  支持之前
- `ArtiRenderer/ArtiChoco/artichoco/scene/README.md` —— ECS 的完整 API 与不变式
- `ArtiRenderer/ArtiChoco/artichoco/renderer/README.md` —— Vulkan / NVRHI 边界、Slang 反射、
  坐标约定、帧与线程模型
- `ArtiRenderer/ArtiChoco/artichoco/core/task/README.md` —— 任务系统（enkiTS 封装）：
  线程编号、`parallelFor` 的 grain size、句柄语义、`TaskGraph`、以及明确不做的四件事
