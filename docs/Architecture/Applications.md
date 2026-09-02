# 应用层

> 三个可执行都是同一个形状：`ArtiChoco::Core` 提供 `main`（`entry_point.cpp`）→ 调用者实现
> `createApplication()` → 往 `Application` 里推 Layer → `Application::run()` 每帧遍历 layer 栈。
> 真正干活的东西都在 `ArtiEngine::Runtime` 里，应用层只有胶水：谁先建、谁后拆、参数从哪来。

## 1. Application / Layer 骨架

```cpp
// ArtiChoco 提供 main()，负责日志初始化、异常兜底、退出码
int main(argc, argv) {
    Logger::init(<exe目录>/logs/ArtiChoco.log, Debug 或 Info);
    auto app = createApplication(argc, argv);   // ← 由可执行自己实现
    app->run();
}

// 每帧
while (running && !window->shouldClose()) {
    window->onUpdate();                       // 事件泵
    for (layer : stack) layer->onUpdate(dt);
    for (layer : stack) layer->onImGuiRender();
    for (layer : stack) layer->onRender();
}
```

`ApplicationCreateInfo` 里的 `window_factory` 决定要不要真窗口：`createSDLWindow` 给
SDL3 + Vulkan surface，`createHeadlessWindow`（默认）不开窗。

## 2. scene_editor

`Tools/scene_editor`。一层 `EditorLayer` 拥有全部东西。

```
EditorLayer                    渲染设备、Renderer、ImGuiHost、SceneRenderer，以及下面这些
├── EditorProject              打开的项目：AssetPipeline + GPUAssetCache
│                              两者生命周期都绑在项目上 —— 换项目要重扫 .meta、重传 GPU 资源
├── EditorContext              World + 选中实体 + Edit/Play 模式 + Play 快照
├── SceneDocument              「哪个场景文件」：新建 / 打开 / 存 / 另存 / 脏标记 / 记住上次
├── EditorCamera               Edit 模式的相机（Play 模式用场景里的 primary）
├── EditorGizmo                ImGuizmo 变换手柄（translate / rotate / scale，local / world）
└── panels/
    ├── HierarchyPanel         实体树、创建 / 删除 / 改父子
    ├── InspectorPanel         选中实体的组件：Transform / Camera / MeshRenderer /
    │                          三种光源 / Environment
    ├── ContentBrowserPanel    Assets/ 树 + 选中项预览
    ├── ProjectSettingsPanel   模态：项目名、StartScene 等
    └── ViewportPanel          场景纹理 + gizmo 覆盖 + 点击 → 拾取 + 资产拖放落点
```

`ProjectManager` 是全局单例（ArtiChoco 的设计），但资产工作区不是 —— 所以
`EditorProject` 持有它们。

### Content Browser 的树

左边一棵树，右边选中项的预览。树是**两个域的显式合并**：文件系统提供「目录里有什么」，
`AssetCatalog` 提供「这些是什么资产」。所以层级是 **目录 → 源文件 → 资产**：源文件节点展开
后是它导出的那些资产，每个资产各自成行、各自可拖拽 —— **拖拽在资产粒度**，不是「主资产」
粒度。引擎自带资产在 `Assets/` 下没有源文件，挂在树末尾单独一个节点下。

没有「当前目录」这个概念，所以也不需要面包屑或返回上级。每帧只枚举被展开的目录，代价随可见
范围走。拖拽 payload 类型是 `ARTI_ASSET_UUID`，数据就是一个 `uint64_t`。

右边的预览栏「选资产」和「选源文件」二选一，这样它只有一个数据源。选源文件时显示路径、状态
（imported / stale / not imported / no importer）和产出资产数 —— **还不能在这里改导入设置**，
那条路目前只有 CLI。

拖进 Viewport 时：**prefab** 按节点树生成实体；**mesh** 生成单个实体，材质用 builtin default。

### Edit / Play

```
enterPlayMode()   snapshot.copyEntitiesFrom(world.scene())   拷快照
                  World::resetClock()                        新会话，帧号和固定步长余额归零
每帧              World::tick(dt)                            和独立 player 同一份代码
exitPlayMode()    world.scene().copyEntitiesFrom(snapshot)   原样拷回
```

所以 Play 期间的改动不落在正在编辑的场景上。快照靠 `Scene::registerComponentCopy<T>()`
注册过的类型 —— 漏注册的组件会被静默跳过（有 warn）。重复调 enter / exit 是空操作。

Play 模式下 gizmo 禁用、编辑器相机不更新（用场景里的 primary 相机），调试线也不提交。

所有换场景的入口都从 `SceneDocument::reset()` 走：那里会退出 Play 模式并清掉选中的实体 ——
快照和选中 ID 指的是马上要被清掉的实体，留着就是悬空引用。

### 编辑器专属的平台代码

原生文件对话框在 `Tools/platform`（`ArtiTools::Platform`），不放进 `ArtiChoco::Platform`
—— 那里是运行时要的东西（窗口、surface），这里是只有编辑器类工具才需要的。布局上
`common/` 放平台中立的接口头，各平台目录放实现，**分派在 CMake 层按平台选源文件**，
所以实现里不带 `#ifdef`。目前只有 Windows 一支（`IFileDialog`）。

## 3. arti_player

`Runtime/player`。一层 `PlayerLayer`，只有胶水。

```
arti_player [options] [<project.artiproj>]
  --project <file>   要跑的项目。不给就用 exe 旁边那个唯一的 .artiproj
  --scene <file>     跑这个场景而不是 ProjectInfo::StartScene（相对路径按项目根解析）
  --stats            调试覆盖层（FPS、draw calls、实体数）
  --help
```

`createApplication()` 里的顺序是刻意的：

1. **解析参数** —— 需要值的选项缺了值就是用法错误（退出码 2），不静默当成开关；给了两个位置
   参数也不猜。
2. **加载项目** —— 在建窗口之前，因为窗口标题要用项目名。
3. **定下场景** —— `--scene` 优先，否则 `StartScene`；然后确认文件真的在。**这一步放在建
   Vulkan 设备之前**：项目没配起始场景、或者场景被删了，是配置问题，该立刻说清楚，而不是
   先花两秒起一个渲染器、开一个窗口，再在里面报错。
4. **判定模式** —— 项目根下有 `catalog.artimanifest` 就走打包模式，没有就走开发模式。
   用「文件在不在」判定而不是加 `--packaged` 开关：双击 exe 的人不会传参数，而这两种模式的
   区别恰好就是那个文件在不在。

用法错误（2）和配置错误（1）分开退出码，脚本里能区分「我调错了」和「项目坏了」。

`PlayerLayer` 建的东西：`RenderDevice` → `Renderer`（`PresentMode::Direct`）→ `AssetRuntime`
→ `GPUAssetCache` → `World` → `SceneRenderer`。`ImGuiHost` **只有 `--stats` 时才建** ——
运行时不该为了「可能要画点调试信息」就常驻一个 UI 上下文（链接是无条件的，但不建上下文就不
花运行时代价）。

`onAttach` 里任何一步失败就保持 `m_ready == false` 并请求关闭应用：一个加载不了场景的播放器
该带着原因退出，而不是留一个永远黑着的窗口。

## 4. asset_tools

`Tools/asset_tools`。无窗口 CLI，命令一览见 [Assets.md](Assets.md#8-cli)。

它链进了渲染类型（`MeshAsset` 里有 `rendering::MeshVertex`，`ArtiSDK::Vulkan` 也在依赖链上）
但**从不创建 `RenderDevice`** —— 没有窗口、没有 GPU。这是 `GPUAssetCache` 留在
`AssetRuntime` 之外的直接原因。

`ArtiEngine` 层的日志通道刻意不走 `Application::get().getLogChannel()`：这一层被 CLI 消费，
那里根本没有 `Application`，取单例会直接断言。所以有一个独立的 `arti::engine::getLogChannel()`
（和 `ArtiScene` / `ArtiAsset` 那些通道一个套路）。

## 5. 从编辑器到可发布产物

```
1. 编辑器里开项目，把模型 / 贴图丢进 Assets/         → reconcile 自动导入
2. 编场景，Project Settings 里设 StartScene          → .artiproj
3. asset_tools pack <project.artiproj> <out>         → 自足的产物目录
4. 把 <out>/ 整个拷走                                → 双击 arti_player.exe
```

`pack` 除了资产，还会把 `asset_tools` **自己 exe 旁边**的运行时文件写进产物：`*.dll`、
`shaders/**`、以及 `arti_player.exe` 本身。所以第 4 步不需要再手动补什么 —— 产物在没装
Vulkan SDK、也没有源码树的机器上能直接跑。

产物布局见 [README.md](README.md#项目打包后)，`pack` 的几条设计决定（拒绝往非空目录写、
先校验完整性、缺 DLL / shader 算失败但缺播放器只 warn、场景为什么还在 `Assets/` 下）
在 [Assets.md](Assets.md#7-打包)。

两个还欠着的：

- **`pack` 只有 CLI 一条路**，编辑器里没有「Build」菜单项。
- **没在真正干净的机器上验过**：clean PATH、回落路径失效、CRT 加载的确实是产物里那一份
  （看过进程的模块列表），但没在一台没装 VC++ Redistributable / 没装 Vulkan 驱动的机器上跑过。
  `vulkan-1.dll` 靠显卡驱动提供，那一份不能也不应该随产物走。

另外：**发布只能用 Release 构建**。Debug 产物链的是调试 CRT（`ucrtbased.dll` /
`MSVCP140D.dll` / `VCRUNTIME140D.dll`），那几个不可再分发，而 staging 拷进产物的是 redist 里的
release CRT。目前没有机制防止用 Debug 构建去 pack。
