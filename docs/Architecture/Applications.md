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
├── EditorContext              World + 选中实体 + Edit/Simulate/Play 三态 + 模拟前的快照
│   └── EditHistory            撤销 / 重做栈（整场景快照，见「撤销」一节）
├── SceneDocument              「哪个场景文件」：新建 / 打开 / 存 / 另存 / 脏标记 / 记住上次
├── EditorCamera               Edit / Simulate 的相机（Play 用场景里的 primary）
├── EditorGizmo                ImGuizmo 变换手柄（translate / rotate / scale，local / world）
└── panels/
    ├── HierarchyPanel         实体树、创建 / 删除（Del）/ 复制（Ctrl+D）/ 改父子
    ├── InspectorPanel         选中实体的组件：Transform / Camera / MeshRenderer /
    │                          三种光源 / Environment / RigidBody / Collider
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

### Edit / Simulate / Play

三种模式的区别只有两条**互相独立**的轴：跑不跑系统、是不是游戏视角。

| | 跑 `World::tick()` | 相机 | gizmo | 调试线（选中轮廓 / 光源线框） |
| --- | --- | --- | --- | --- |
| **Edit** | 否 | 编辑器相机 | 开 | 画 |
| **Simulate** | **是** | **编辑器相机** | **开** | **画** |
| **Play** | 是 | 场景的 primary 相机 | 关 | 不画 |

也就是说 **Simulate = 系统在跑，但你还坐在编辑器里**：可以自由飞、可以点选正在下落的盒子、
可以在 Inspector 里看它的数值在变。Play 是「以游戏的方式看」。这正是 Unreal 的 Play / Simulate
之分，而对调物理参数来说它不是可选项。

`EditorContext` 因此给出两个**语义明确**的查询，而不是让调用方去比较枚举：

```cpp
bool isSimulating() const;   // mode != Edit —— 跑系统
bool isGameView() const;     // mode == Play —— 场景相机 / 无 gizmo / 无调试线
```

**这里刻意没有 `isPlaying()`。** 六个调用点分属上面两条轴，只有两种模式时恰好重合；留一个
含混的查询在那儿，下一个人就会拿它去判断「要不要画 gizmo」，于是 Simulate 下 gizmo 就没了。
同理 `onUpdate` 里那两件事是两个独立的 `if`（Simulate 下「系统在跑」和「相机是编辑器的」同时
成立），**不是** if-else。

快照机制三种模式共用一套：

```
enterMode(Simulate/Play)  snapshot.copyEntitiesFrom(world.scene())   拷快照
                          World::resetClock()                       新会话：帧号和固定步长余额归零
每帧                      World::tick(dt)                           和独立 player 同一份代码
exitToEdit()              world.scene().copyEntitiesFrom(snapshot)  原样拷回
```

所以模拟期间的改动（**物理写的 transform 也算**）不落在正在编辑的场景上，「模拟一下再撤销」是
免费的。快照靠 `Scene::registerComponentCopy<T>()` 注册过的类型 —— 漏注册的组件会被静默跳过
（有 warn）。重复进同一个模式是空操作。

**只允许 Edit ↔ Simulate 和 Edit ↔ Play**，不做 Simulate ↔ Play 的直接切换（那要先回答「切过去
之后快照算谁的」）。工具栏两个按钮，激活的那个变成 Stop、另一个禁用。真要求直接切换时
`enterMode()` 也不会把快照弄乱：先回 Edit 再进另一个模式。

模拟期间 **transform 归物理**，所以拖 gizmo 推不动正在模拟的物体 —— 那是有意的：东西怎么动由
物理引擎决定（见 [Scene.md](Scene.md) 的 3.1）。

所有换场景的入口都从 `SceneDocument::reset()` 走：那里会无条件退到 Edit 并清掉选中的实体 ——
快照和选中 ID 指的是马上要被清掉的实体，留着就是悬空引用。

### 编辑器专属的平台代码

原生文件对话框在 `Tools/platform`（`ArtiTools::Platform`），不放进 `ArtiChoco::Platform`
—— 那里是运行时要的东西（窗口、surface），这里是只有编辑器类工具才需要的。布局上
`common/` 放平台中立的接口头，各平台目录放实现，**分派在 CMake 层按平台选源文件**，
所以实现里不带 `#ifdef`。目前只有 Windows 一支（`IFileDialog`）。

`View / VSync` 切垂直同步，运行时重建 swapchain。启动参数 `--no-vsync` 一样。

### 键盘快捷键

两个分派点，都在 `onImGuiRender()` 里、**在画面板之前**：

| 分派点 | 键 | 做什么 |
| --- | --- | --- |
| `EditorLayer::handleShortcuts()` | `Ctrl+N` / `Ctrl+O` | 新建 / 打开场景 |
| | `Ctrl+S` / `Ctrl+Shift+S` | 存 / 另存场景 |
| | `Ctrl+D` | 复制选中实体（连子树） |
| | `Ctrl+Z` / `Ctrl+Y` | 撤销 / 重做（见下一节） |
| | `Del` | 删除选中实体（连子树） |
| `EditorGizmo::handleShortcuts()` | `W` / `E` / `R` | 手柄切 translate / rotate / scale |

两点实现上的取舍：

- **带修饰键的走 `ImGui::Shortcut()` + `RouteGlobal`**，不走 `IsKeyChordPressed()`：前者带焦点
  路由，在输入框里打字不会误触发。`Ctrl+S` 和 `Ctrl+Shift+S` 不需要排先后 —— ImGui 要求
  修饰键**精确匹配**，按着 Shift 时 `Ctrl+S` 那条根本不成立。
- **光秃秃的字母 / 功能键额外靠 `GetIO().WantTextInput` 让路**：没有修饰键兜底，不挡的话在
  Inspector 里改个名字就顺手把手柄模式换了（`W` / `E` / `R`），或者想删掉一个打错的字符却
  删掉了整个实体（`Del` —— 那一下不可撤销，所以除了 `Shortcut()` 的路由再显式挡一道）。
  编辑器相机的 WASD 走 `core::Input`，是另一条路。

`Del` 的语义固定是「删掉选中的实体」，不随焦点面板变 —— 整个编辑器只有一份选中状态
（`EditorContext::m_selected_entity`），Content Browser 那边也没有删除操作。

每个快捷键都和对应菜单项**共用同一个前提判断**（`canChangeScene()` / `canSaveScene()` /
`canDuplicateSelection()` / `canDeleteSelection()`）。「菜单里是灰的、快捷键却能按」是最难查的
那种不一致，所以判断只写一处。

**模拟中不许存场景**（`canSaveScene()` 判 `isSimulating()`，菜单项和 `Ctrl+S` 都吃这条）。
理由：Simulate / Play 期间物理一直在往场景里写 transform，而 `World::saveScene()` 序列化的就是
那个活场景 —— 存下去等于把盒子掉落后的姿态盖在编辑好的场景上。编辑期那一份原样躺在快照里，
Stop 之后才回来。新建和打开则不受限：它们从 `SceneDocument::reset()` 走，那里会无条件退到 Edit。

`Del` 那一下**现在能撤销了**，但那道 `WantTextInput` 的显式防线不要拆：能救回来仍然不如别误删。

复制和删除都走 `HierarchyPanel::request*()`，真正的改动推迟到下一次 `draw()` 的开头执行 ——
不在遍历实体、画着 ImGui 树的中途改 registry，而且**刻意放在 `ImGui::Begin()` 之前**：
Hierarchy 折叠着的时候 `Begin()` 返回 false 会直接 return，放在后面就会让 `Ctrl+D` / `Del`
在那种状态下静默失灵。删完按「选中的还在不在」清选中，而不是比对「删掉的是不是选中的那个」
—— `destroyEntity()` 连整棵子树一起删，删一个祖先也会带走选中的实体。

Play 模式下两个都不给（判 `isGameView()`），Simulate 下都允许。Simulate 下的代价各不相同：
复制出来的实体没有刚体（物理世界不会为它重建），而删除是安全的 —— `PhysicsSystem` 已经处理了
「写回时实体没了」，且 Stop 之后场景从快照恢复，模拟中删掉的实体会自己回来。

### 撤销 / 重做

`EditHistory`（`Tools/scene_editor/src/edit_history.h`），挂在 `EditorContext` 上。一条历史项 =
**整个场景序列化出来的那段文本** + 当时选中的实体。

**为什么是整场景快照而不是命令模式**：场景的写入点有 42 处以上，全都是把组件字段的地址交给
ImGui 控件、控件当场改掉它（`inspector_panel.cpp` 里那一片 `drawFloatRow(..., &light->intensity,
...)`），中间没有任何可以插钩子的层。命令模式的接入成本是「控件数 × 一个命令类」，而且每加一个
字段都要记得再加一个 —— 漏一个的症状是「这个字段撤不回来」，只有手动试到那个字段才会发现。
快照式的接入成本和字段数无关。代价是粒度只到「整个场景」（一次交互 = 一条历史项），
以及每条历史项都是一份全量拷贝（上限 64 条 / 64 MiB，从最旧的一端丢）。

**为什么存序列化文本而不是 `Scene` 克隆**（克隆更快更省，Play 模式的快照走的就是那条路）：
文本能直接比较，于是「这次交互到底改了什么没有」变成一行代码，而且是精确的。由此得到两个连锁
的好处 —— 一是**不可能产生空历史项**（点一下不改东西的控件、每帧无条件重写同一个值的钳制代码
都不会污染历史栈），二是**因此提交时机允许写得粗**。第二条才是真正的收益：
`inspector_panel.cpp` 一行都不用改。

「文本相同 ⇔ 场景相同」这一条成立，靠的是 `SceneSerializer` 把实体按 UUID、组件按类型名都排过序
（规范形式）。`ctest` 里的 `scene_snapshot_smoke` 用「同样的内容、相反的创建顺序，dump 必须逐字节
相同」钉着它 —— 那条塌了，编辑器里会凭空冒出一堆「按了 Ctrl+Z 却什么都没变」的历史项。

提交时机（都在 `EditorLayer::onImGuiRender()` 的帧末，此时面板和 gizmo 都画完了）：

| 信号 | 覆盖 |
| --- | --- |
| `ImGui::IsAnyItemActive()` 的**下降沿** | 所有 Inspector 控件、改名、菜单项、按钮、下拉框。一次拖拽从按下到松开 `ActiveId` 一直在，只有松开那帧掉沿 —— 所以**整条拖拽合成一条历史项** |
| `EditorGizmo::isUsing()` 的**下降沿** | gizmo 拖拽（它不是 ImGui 的 item，ImGuizmo 自己管状态） |
| `EditHistory::requestCommit()` | 只给**不经过 ImGui 控件**的改动：延迟到下一帧落地的复制 / 删除、右键菜单里的建实体、拖资产进 Viewport。多报一次是无害的 —— 没变就不会压历史项 |

两条限制，都和 `canSaveScene()` 同源：

- **模拟中不记历史、不许撤销。** 物理每个固定步都在写 transform，记下去就是一栈「盒子又掉了两
  厘米」。而模拟期间的改动本来就落在快照上、Stop 就原样回来，所以「撤销一次模拟」已经是免费的。
- **交互进行中不许撤销**（`canEditHistory()` 判 `IsAnyItemActive()` 和 `isUsing()`）：一边按着
  拖动框一边按 `Ctrl+Z`，ImGui 的 `ActiveId` 还指着那个控件，而它背后的组件已经被整个换掉了。

选中一起进历史，取的是**提交那一帧开始时**的选中（`beginFrame()`）。删除是在
`HierarchyPanel::draw()` 开头落地的，落地时会清掉选中 —— 取帧末的话「删掉 E」这条历史项记的
选中是空，撤销回去 E 回来了却没被选中；取帧初记的是 E，撤销回去直接选上。
选中变化本身不产生历史项（文本没变）。

**脏标记由历史推导**：`SceneDocument::isDirty()` = 当前状态编号 ≠ 存盘时记下的编号。所以它是
精确的（以前只有「拖资产进 Viewport」那一处置位，Inspector 改数、拖 gizmo、建 / 删实体全都不算
脏），而且**撤销回存盘时的那个状态会自动变回干净**。工具栏上模式后面那个 `*` 就是它。

**已知边界：只有序列化过的组件能撤销。** 现在八个引擎组件既注册了拷贝也注册了序列化，所以此刻
没有差别；但「注册了拷贝、没注册序列化」的运行时组件是允许存在的（见 [Scene.md](Scene.md) 第 6
节），那种组件撤不回来，而且不会报错。**「撤销对某个字段不生效」的第一嫌疑人是面板的缓存**：
Inspector 里几个 UUID 输入框每帧都会把缓存的文本写回组件，缓存不跟组件对账的话撤销会被它吃掉
（Environment 那个已经修了，加组件时照 MeshRenderer 的样子做）。

## 3. arti_player

`Runtime/player`。一层 `PlayerLayer`，只有胶水。

```
arti_player [options] [<project.artiproj>]
  --project <file>   要跑的项目。不给就用 exe 旁边那个唯一的 .artiproj
  --scene <file>     跑这个场景而不是 ProjectInfo::StartScene（相对路径按项目根解析）
  --stats            调试覆盖层（FPS、draw calls、实体数）
  --no-vsync         关掉垂直同步（surface 支持的话走 IMMEDIATE）
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

产物的运行要求（Windows 10+ x64、支持 Vulkan 1.3 的驱动、目录可写）见
[README.md](README.md#运行要求打包产物)。**不支持旧设备是明确的产品决定** ——
Vulkan 1.3 这个下限是刻意的，不要为更老的设备加回落路径。

还欠着的：

- **`pack` 只有 CLI 一条路**，编辑器里没有「Build」菜单项。
- **没在真正干净的机器上验过**：clean PATH、回落路径失效、CRT 加载的确实是产物里那一份
  （看过进程的模块列表），但没在一台没装 VC++ Redistributable / 没装 Vulkan 驱动的机器上跑过。
- **发布只能用 Release 构建**。Debug 产物链的是调试 CRT（`ucrtbased.dll` / `MSVCP140D.dll` /
  `VCRUNTIME140D.dll`），那几个不可再分发，而 staging 拷进产物的是 redist 里的 release CRT。
  目前没有机制防止用 Debug 构建去 pack。
- **还粗糙的一处**：播放器是 console 子系统，双击会多开一个黑框，失败信息也只在那里。
  （日志写不出来已经不再致命了，只会警告一次并降级成控制台输出。）
