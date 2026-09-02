# 物理：接入 Box3D，让东西掉下来

| | |
| --- | --- |
| **状态** | 已完成并验收、已 push |
| **创建** | 2026-09-02 |
| **最后更新** | 2026-09-02 |
| **涉及仓库** | ArtiEngine（全部改动都在这里；ArtiRenderer / ArtiChoco 不动） |
| **目标** | 刚体物理：盒子会掉下来、能堆叠；`FixedUpdate` 第一次真跑；并加一个 **Simulate 模式**（边跑边坐在编辑器里看） |

---

## 交接区

> **全文唯一允许改动的段落。每次收工前更新这里。**

**结论：五个阶段全部完成，端到端验收 11 条都过了。** 盒子会掉、会堆、Stop 回原位；Simulate 和
Play 两种模式按两条轴各自正确；组件存盘重开还在；`arti_player` 和编辑器的数值一模一样。
九个 commit 已经 push 到 `origin/main`（`6ebc092..24609d8`）。

- box3d 在 `third_party/box3d`，指针 `47d7f7c`（`v0.1.0-21-g47d7f7c`），`.gitmodules` 里有 `branch = main`。
- `third_party/CMakeLists.txt` 里 `add_subdirectory(box3d)`；`box3d` target 建得出来，产物是
  `build/lib/box3dd.lib`（`d` 后缀是它自己设的 `DEBUG_POSTFIX`）。
- `ArtiEngine/runtime/tests/physics_smoke.cpp` + `ArtiEngine/CMakeLists.txt` 的 `BUILD_TESTING` 分支。
  `ctest` 两个测试都过（`physics_smoke` 0.01s、`asset_pipeline_smoke` 0.44s）。
- 1.1 那条的标题还写着「并 pin tag」，是 D1 被改写之前的残留，正文（`git submodule add -b main`）是对的。
- 阶段 1～3 都已提交，**还没 push**。

**阶段 2 的实际形状**（组件字段名后面 3.2 建 shape 时要一一对上）：

- `RigidBodyComponent`：`type`（`Type::{Static, Kinematic, Dynamic}`，默认 Dynamic）、
  `gravity_scale`（1）、`enable_sleep`（true）。枚举**不用** box3d 的 `b3BodyType` ——
  `components.h` 是引擎公开头，编辑器和 player 都包含它，不该让 box3d 的类型扩散出去。
- `ColliderComponent`：`shape`（`Shape::{Box, Sphere, Capsule}`）、`half_extents`（0.5³）、
  `radius`（0.5）、`half_height`（0.5）、`density`（1）、`friction`（0.3）、`restitution`（0）。
  三种形状的尺寸字段**都常驻**（切回来值还在），面板上只显示当前形状用得到的那几行。
- 序列化键是 PascalCase（`Type` / `GravityScale` / `EnableSleep` / `Shape` / `HalfExtents` /
  `Radius` / `HalfHeight` / `Density` / `Friction` / `Restitution`），**枚举按名字写不按数字**
  （老场景插一项枚举也不会让值悄悄变成另一种；名字不认识时退回默认值，和缺键一样处理）。

**阶段 3 的结果**（数值和画面都验过）：

- `ArtiEngine/runtime/physics_system.{h,cpp}`，pimpl，`b3*` 一个都没出现在头里；
  `artiengine_runtime` **PRIVATE** 链 `box3d`（下游看不见 box3d 的头）。系统注册在 `World` 的
  构造函数里，全工程唯一一处。
- 测试场景 **`projects/Assets/Scenes/physics_test.artiscene`**（留在仓库里，阶段 4 接着用）：
  三个单位立方体从 y = 0 / 1.5 / 3 掉到静态地面上，**外加三个「该被跳过」的实体** ——
  缩放过的地面视觉体、只有 Collider 的球、带父级的头盔子节点。
- 无头跑 4 秒的实测：Box A/B/C 落在 `y = -0.866 / 0.133 / 1.132`（算出来的预期是
  -0.866 / 0.134 / 1.134），水平漂移 < 0.03，朝向仍是单位四元数（w 没放错位置），再跑 2 秒
  一动不动；被跳过的三个原地没动。日志正好是 `Physics world built: 4 bodies (3 entities skipped)`
  加三条对得上的 warn —— **所以 4.5 那两条 warn 其实已经看到了**，到时候只是在编辑器里再确认一次。
- `PrintWindow` 抓播放器窗口：t≈1s 和 t≈4s 两张图一模一样（堆好了不抖）。

**做法上和文档不一样的三处（都是往稳的方向改，不是绕过）**：

1. **重建信号用「帧号不再单调递增」而不是 `frameIndex == 0`。** 信号源还是 D5 说的
   `resetClock()`，但比较单调性顺带就是文档要求的「这一帧已经建过了」那道保护 ——
   `FixedUpdate` 一帧里可能被调多次（追帧），写成 `== 0` 还得再加一个标志位。
2. **「建了 N 个 body」那条日志留下来了**（`debug` 级，一次模拟一条），没按 3.2 的
   「验完删日志」删。理由：ArtiScene 自己就在 `debug` 打「Copied 10 entities」这类行，这条是
   同一类东西，而且下次物理出问题第一个要看的就是它。
3. **地面在场景里是两个实体**：一个缩放过的立方体只做视觉、一个不缩放的空实体只做碰撞体
   （半长 8 × 0.5 × 8，上表面和视觉地面对齐）。这是 D3 的直接后果 —— 缩放过的实体不参与模拟，
   而「一块大地面」在视觉上必须缩放。**给场景搭物理的人都会撞上这条**，所以记在这儿。

**4.3 的验收里那句「能把盒子推一把」作废了 —— 用户拍板：拖 gizmo 不该推物体。** 理由是物理归
物理引擎管。所以模拟期间 transform 单向地由物理写，`TransformComponent` 上的 gizmo 编辑会被下一
个固定步长覆盖（body 睡着时看起来能拖，一醒就弹回去），这是**有意的行为**，不是待修的 bug。
4.3 实际的验收是「gizmo 在、能选中、能看数值在变」。**别为了让 gizmo 能推而塞一个
`b3Body_SetTransform`** —— 它的文档明确说那是传送，会有性能和行为问题；真要做也是「拖动时给 body
设速度」，而且是另一件事。

**阶段 4 的形状**：`Mode { Edit, Simulate, Play }`；`isPlaying()` 删掉了，换成
`isSimulating()`（`mode != Edit`，跑系统）和 `isGameView()`（`mode == Play`，场景相机 / 无 gizmo /
无调试线）；`enterPlayMode` / `exitPlayMode` → `enterMode(Mode)` / `exitToEdit()`；
`updatePlay` → `updateSimulation`。六个调用点全按 D8 那张表改完了。

**其中一处不是简单替换**：`onUpdate` 里原来是 `if (isPlaying()) updatePlay(); else
updateEditorCamera();` —— 两条轴混在一个 if-else 里。Simulate 下这两件事**同时**成立
（系统在跑，相机还是编辑器的），所以拆成了两个独立的 if。**别写回 if-else。**

**阶段 4 的验收怎么过的**：三种状态的工具栏、Simulate 那半边（编辑器相机 + gizmo + 调试线都在
而盒子在掉）、Stop 回原位，都由用户在编辑器里肉眼确认。Play 那半边我也抓到图了：工具栏是
`Stop` + 灰掉的 `Simulate` + `[Play]`，视口用场景相机，选中 Box C 也**没有** gizmo / 选中轮廓，
而 Inspector 里 Box C 的 Translation 是 `(0.020, 1.132, -0.022)` —— 和无头跑出来的数字一模一样，
所以 D2 那句「编辑器和 player 跑的是同一份」是实测过的，不是推理出来的。

**阶段 1 要确认的三件事 —— 已确认**（都是从 `third_party/box3d/include/box3d/` 的头里读出来的，
不是从文档抄的）：

1. **球和胶囊的创建函数**：`b3CreateSphereShape(bodyId, &shapeDef, &sphere)` 吃
   `b3Sphere{ b3Vec3 center; float radius; }`；`b3CreateCapsuleShape(bodyId, &shapeDef, &capsule)` 吃
   `b3Capsule{ b3Vec3 center1; b3Vec3 center2; float radius; }` —— **是两个半球心 + 半径，不是
   「半高 + 半径」**，所以组件里存半径 + 半高的话，建 shape 时要换算成 `center1/2 = ±half_height * y`。
   另外还有 `b3CreateTransformedHullShape` / `b3CreateMeshShape` / `b3CreateHeightFieldShape` /
   `b3CreateBakedCompoundShape`，v1 都不用。
2. **`b3World_GetBodyEvents` 的字段**：返回 `b3BodyEvents{ b3BodyMoveEvent* moveEvents; int moveCount; }`，
   每条是 `b3BodyMoveEvent{ void* userData; b3WorldTransform transform; b3BodyId bodyId; bool fellAsleep; }`。
   单精度下 `b3WorldTransform` 就是 `b3Transform{ b3Vec3 p; b3Quat q; }`，所以**写回直接读
   `move.transform.p` / `.q`，不用再逐个 `b3Body_GetPosition`**。
3. **CMake 选项**：`BOX3D_SAMPLES` / `BOX3D_BENCHMARKS`（不是 `BENCHMARK`）/ `BOX3D_UNIT_TESTS` /
   `BOX3D_DOCS` / `BOX3D_BUILD_SHADERS` / `BOX3D_PROFILE` / `BOX3D_VALIDATE` **全都定义在它自己的
   `if(PROJECT_IS_TOP_LEVEL)` 里面**，作为 submodule 消费时这些变量根本不存在 —— 1.2 那条「先把开关
   关掉」无事可做。消费者能看到的只有 `BOX3D_DISABLE_SIMD` / `BOX3D_COMPILE_WARNING_AS_ERROR` /
   `BOX3D_DOUBLE_PRECISION`，都默认 OFF（`build/CMakeCache.txt` 里就这三条，外加
   `box3d_IS_TOP_LEVEL:STATIC=OFF`）。顺带：`BOX3D_VALIDATE`（重校验）也因此对我们是关的。

**顺手确认的第四件事**（阶段 2.1 和 3.2 直接要用）：材质属性在 `b3ShapeDef` 上**嵌了一层** ——
`shapeDef.density` 是平的，但 friction / restitution 在 `shapeDef.baseMaterial` 里
（`b3SurfaceMaterial`，还带 `rollingResistance` / `tangentVelocity` / `userMaterialId`）。
`b3BodyDef` 那边 D4 要的三个字段名字正好对得上：`type` / `gravityScale` / `enableSleep`
（另有 `sleepThreshold` / `motionLocks` / `isAwake`，v1 不碰）。

`physics_smoke` 已经把上面这些都跑过一遍了（三种形状各建一个、逐步消费 `GetBodyEvents`、
`userData` 塞满 64 位再取回、拆世界后查 `b3GetByteCount()` 回到基线）—— 这就是它作为
「`--remote` 之后第一个报警的东西」的价值所在，不只是让盒子掉。

**验证手段（以后接着用）**：

- **抓编辑器/播放器窗口**：`build/gui.ps1`（不进仓库，`build/` 是 ignore 的）三个动作 ——
  `-Action shot` 用 `PrintWindow(hwnd, hdc, 2)` 抓客户区（不用把窗口弄到前台）、
  `-Action click -X -Y` 客户区坐标点一下（**会抢焦点**）、`-Action keys -Text` 发按键。
  P/Invoke 那段单独放在 `build/gui_src.cs`：pwsh 7 里 `Add-Type` 编译带 `System.Drawing`
  的 C# 会因为程序集转发报 CS1069，所以位图和 `Graphics` 都在 PowerShell 侧建，C# 只留纯
  P/Invoke 签名。
- **编辑器没有命令行参数、也不会自动打开上次的项目**（`EditorLayer::onAttach` 不开项目，
  `Open Project...` 只能走文件对话框；`last_open_scene` 是**项目内**的记忆）。所以脚本化地
  从零打开一个项目要驱动原生对话框 —— 想避开的话，就让人先把项目开着。
- 阶段 2.2 是用一个临时程序验的（`World::saveScene` / `loadScene` 真存真读，25 条断言，验完删）：
  照 `build/compile_commands.json` 里 `asset_pipeline_smoke` 的编译行编译、照 `ninja -t commands`
  的链接行链接，就不用往 CMake 里加临时 target。顺手确认到的两件事：**yaml-cpp 写出来的 float
  能精确读回**（`0.6f` / `0.4f` 都相等），以及**同一个实体的组件在 YAML 里是按类型名字母序排的**
  （`artiengine.collider` 在 `artiengine.rigid_body` 前面）。

**决定记录**（时间倒序，新的加在最上面）：

- 2026-09-02 用户拍板：**拖 gizmo 不推物体，物理交给物理引擎**。作废了 4.3 验收里「能把盒子推
  一把」那句，见上面那段。
- 2026-09-02（执行中我改的，不是用户拍板）：重建物理世界的信号从 `frameIndex == 0` 改成
  **帧号不再单调递增**。信号源仍是 D5 说的 `resetClock()`，只是顺带把「这一帧已经建过了」那道
  保护并进同一个判断里了。
- 2026-09-02 用户又拍两条：**box3d 不 pin 版本、跟 main**（改写了 D1）；
  **加第三种模式 Simulate**（新增 D8，阶段 4 重写）。
- 2026-09-02 用户拍板：**用 Box3D**（不是 Jolt），其余按我的建议 —— transform 在模拟期归物理、
  第一版只做球 / 盒 / 胶囊、不做射线查询和触发器。

**踩到的坑**：

- **box3d 强制静态 CRT，必须在我们这边掰回来。** 它的 CMakeLists 顶上无条件
  `set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")`（也就是 /MT，
  注释说是为了方便查内存泄漏），而本项目其余部分是 /MD —— 两者混进一个二进制会在链接期或运行期炸。
  已经在 `third_party/CMakeLists.txt` 里用 `set_target_properties(box3d PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")` 修掉，理由写在那儿的注释里。
  **不要以为「clang 走 GNU 前端所以 MSVC_ 那套无效」** —— 实测 CMake 在这条工具链上照样把它翻译成
  `--dependent-lib=`。现在 `build/compile_commands.json` 里 box3d 那 50 条都是
  `--dependent-lib=msvcrtd`，和其余部分一致。
- **box3d 会往我们的源码根设 `FETCHCONTENT_BASE_DIR`**：它写的是
  `set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.fetchcontent-cache" CACHE PATH ...)`，而
  `CMAKE_SOURCE_DIR` 是**我们的**根。今天无害（只有 top-level-only 的 `BOX3D_PROFILE` 会真的下载
  Tracy，所以那个目录根本没被创建），但哪天开了会 FetchContent 的东西，仓库根会多一个没进
  `.gitignore` 的目录。

---

## 背景与现状

### 为什么现在做物理

`World::tick()` 每帧都在调 `runSystems(FixedUpdate)`，而**全工程 `addSystem` 出现 0 次** ——
那个 stage 和它背后的 `FixedTimestepAccumulator` 从第一天起就在空转。物理正是它存在的理由。

而且 `ArtiEngine::Runtime` 当初就是为这件事拆出来的，CMakeLists 里的原话：

> 以后 Runtime 要加依赖（脚本 VM、物理）不必让所有消费 Engine 数据类型的目标跟着背。

所以物理放进 `ArtiEngine::Runtime`、box3d 成为它的依赖，是既有设计的兑现，不是新决定。

**附带影响**：`ArtiTools::Asset` 链 `ArtiEngine::Runtime`，所以 `asset_tools` 会链上 box3d
但永远不建物理世界 —— 和它链了 Vulkan 却从不建 `RenderDevice` 是同一种情况。可以接受。
真嫌它重的话，出路是把 `AssetPipeline` 从 Runtime 上摘下来，那是另一件事。

### 时间步长正好对上

`FixedTimestepAccumulator` 的默认值是 **1/60**，而 Box3D 的文档推荐的正是
`timeStep = 1/60`、`subStepCount = 4`（内部相当于 240 Hz 求解）。两边不用互相迁就。

累加器还有个 `alpha()`（当前余额占一个固定步的比例）和 `max_frame_time = 0.25`（追帧上限），
两个都还没人用 —— `alpha()` 是将来做渲染插值的接缝。

### Box3D 是什么（2026-06-29 发布）

Erin Catto（Box2D 作者）在 Kintsugiyama 做的 3D 刚体引擎，MIT。血缘不是「Box2D 加一维」：
起点是 Dirk Gregorius 对 Valve Rubikon 的业余复刻，之后几乎所有 API、数据结构和算法都换成了
Box2D 的，只在凸包生成和几个碰撞例程里留着原来的痕迹。

**为什么选它而不是 Jolt**（用户已拍板，这里记理由）：

- **C17，除 C 运行时（和 Unix 下的 libm）外无依赖**。对一个自己写引擎的项目，一个能整体读懂的
  C 库比一个更成熟但更庞大的 C++ 库更值。
- **不透明 id（`b3BodyId`）而不是指针**，内部数据导向布局。和这个项目的口味一致。
- **`b3World_GetBodyEvents()`** 直接给出「这一步动过的 body」的连续数组（带 userData、transform、
  `fellAsleep`），正好是「写回引擎 transform」需要的形状 —— 文档明确说不要每帧遍历所有 body。
- CMake + `FetchContent` / `add_subdirectory` / `find_package` 三条路都支持。

**代价（要接受的）**：

- **alpha**。v0.1 刚打标签，目标 v1.0。API 会变，我们得跟着改。成文手册还在写（头文件有完整
  Doxygen）。**PR 目前关闭**，遇到 bug 只能提 issue 等上游。
- **性能不是最快的那个**。godot-rapier 维护者做的对比（vendor-run，注意立场）里 3D 五个场景：
  金字塔 Rapier 19 ms / Jolt 24 / Box3D 25；混合堆 10 / 11 / 14；撞击 12 / 11 / 17；
  查询 5.6 / 22 / 6.3；关节网格 11.4 / **Jolt 不稳定跑不完** / 11.2。
  Box3D 那一列是**单线程**跑出来的（binding 侧的问题），而且没有一个场景被标为不稳定。

### 现有代码里和物理相关的事实

| 事实 | 影响 |
| --- | --- |
| `TransformComponent` 是**可变**的（translation / rotation 四元数 / scale） | 物理写回它 |
| `WorldTransformComponent` 是**场景所有、只读**的派生量（world / local / parent_id / dirty） | 物理**不能**写它 |
| 物理在世界空间工作，而 `TransformComponent` 是局部的 | 见 D3 的限制 |
| `EditorContext` 进 Play 时快照场景、Stop 时拷回 | 物理改的 transform 会被自动还原，不用自己存 |
| Edit 模式**不调** `World::tick()`（只有 `updatePlay` 调） | 编辑模式下物理不动东西，天然正确 |
| `resetClock()` 在 `enterPlayMode()` 里调，把 `frameIndex` 归零 | 见 D5，用它当「重建物理世界」的信号 |
| 第三方 git 仓库在本项目里一律是 **submodule**（ImGuizmo / entt / nvrhi / spdlog / …） | box3d 也走 submodule |

---

## 设计决定

### D1 · box3d 走 submodule，**跟 main 而不是 pin tag** —— 已定（用户拍板）

放在 `ArtiEngine/third_party/box3d`，和 ImGuizmo 一样在 `third_party/CMakeLists.txt` 里
`add_subdirectory` 进来，并加进根 `CMakeLists.txt` 那个「少一层 submodule 就在配置期直接报」的
检查列表。

`.gitmodules` 里声明 `branch = main`：

```
[submodule "third_party/box3d"]
	path = third_party/box3d
	url = https://github.com/erincatto/box3d.git
	branch = main
```

**先澄清一个容易误解的点**：git submodule **永远**记录一个具体 commit，没有「运行时跟最新」这种
模式。所以「不 pin 版本」的实际含义是 —— 指针指向 `main` 的当前 tip（而不是 `v0.1.0` 那个 tag），
升级用 `git submodule update --remote third_party/box3d` 拉到新 tip、然后**把新指针作为一个显式
commit 提交**。

也就是说：**我们仓库的可复现性不受影响**（任何人 clone + `submodule update` 拿到的都是我们记录的
那个 commit），变化的只是「升级」这个动作的频率和随意程度。原来那条 pin tag 的理由（怕
`git submodule update` 之后编译不过）站不住 —— 普通的 `submodule update` 只会拉到我们记录的
commit，只有显式 `--remote` 才会前进。

跟 main 的真实代价：某次 `--remote` 之后上游的 API 变了，我们得跟着改，而**上游 PR 关闭**、
只能提 issue 等或者自己适配。缓解手段是阶段 1.3 那个 `physics_smoke` —— 它是纯 C 的、不碰引擎，
所以升级之后第一个报警的就是它。

**升级的规矩**：`--remote` 之后必须过一遍 `physics_smoke` 和端到端验收，指针单独一个
`chore(deps)` commit（和 `推进 ArtiRenderer 到 <sha>` 一个格式）。ImGuizmo / imgui 已经是
`branch =` 的先例，所以这条路子在本项目里不是新的。

### D2 · 物理是 `ArtiEngine::Runtime` 的一部分 —— 已定

不新开 target。理由见「背景」—— Runtime 当初就是为此拆出来的。

物理系统本体是一个 `SceneSystem` 子类，注册在 `World` 的构造函数里（和
`registerSceneComponents()` 同一处）。**全工程只有这一处注册** —— 编辑器 Play 模式和独立
player 因此跑的是同一份，不会出现「编辑器里能掉、exe 里不动」。

### D3 · 模拟期间物理拥有 transform，但**只对没有父级的实体** —— 已定

写回路径：`b3World_GetBodyEvents()` → 按 `userData` 找到实体 → 写 `TransformComponent` 的
translation 和 rotation。**不动 scale**（物理不改缩放）。

**限制：物理体只作用于没有父级的实体。** 物理在世界空间算，而 `TransformComponent` 是局部的；
有父级时写回需要拿父级的世界逆矩阵反算，那是第二个问题。v1 的边界画在这里，遇到带父级的
物理实体**记一条 warn 并跳过** —— 静默跳过会让人以为物理坏了。

同理，**非单位缩放**的物理实体也 warn：Box3D 的形状尺寸是在组件里显式写的，不从 transform 的
scale 推，所以缩放过的实体会出现「看起来这么大、碰撞体那么大」。

### D4 · 两个组件，都必须有 —— 已定

- `RigidBodyComponent`：`type`（Static / Kinematic / Dynamic）、`gravity_scale`、`enable_sleep`
- `ColliderComponent`：`shape`（Box / Sphere / Capsule）+ 尺寸 + `density` / `friction` / `restitution`

分成两个而不是一个，是跟 Box3D 自己的结构对齐（材质属性挂在 shape 上，好让复合体各部分不同 ——
它文档举的例子是配重偏后的车辆）。将来做复合体时，一个 body 挂多个 collider 是自然的扩展。

**v1 里两个都必须有才会被模拟。** Unity 那种「只有 collider 就当静态碰撞体」的便利先不做 ——
隐式创建 body 会让「为什么这个东西会挡住我」变得不好查。缺一个就 warn。

材质属性放在 collider 上（跟 Box3D 一致），不放在 body 上。

### D5 · 用 `frameIndex == 0` 当「重建物理世界」的信号 —— 已定，但是个将就

物理系统需要知道「一次新的模拟开始了」，好把 Box3D 世界拆掉重建。而 `SceneSystem` 的
`onAttach` 只在 system 被添加时调一次，`copyEntitiesFrom` 明确「不动目标场景已有的系统」。

现成的信号是 `resetClock()` —— `enterPlayMode()` 会调它、`loadScene()` 也会调，两处都正好是
「该重建」的时刻。它把 `frameIndex` 归零，而 `UpdateContext::frameIndex` 传到系统手里。

所以 v1 的做法是：**`onUpdate` 里看到 `context.frameIndex == 0` 就重建世界**。

这是个将就，说清楚：更干净的做法是给 `SceneSystem` 加一个 `onSimulationStart()` 虚函数，
但那要改 ArtiChoco。如果这个信号后来变脆（比如有人加了别的 `resetClock()` 调用点），
就该去加那个虚函数，**不要在这里叠补丁**。

### D6 · 不做射线查询、触发器、关节 —— 已定

v1 只要「盒子掉下来、落在地面上、能堆叠」。射线查询和触发器是脚本系统才需要的东西
（现在没有脚本，查到了也没人消费）；关节要先有一个能编辑关节的 UI，那是另一件事。

Box3D 这些都有（`b3Shape_RayCast` 之类、sensor 系统、六种关节），随时能加。

### D7 · 单线程，先不接 Box3D 的任务系统 —— 已定

`worldDef.workerCount` 默认 1、任务回调为空。接多线程要把 `enqueueTask` / `finishTask` 桥到
ArtiChoco 的 `TaskSystem`（enkiTS），那是一件独立的事，而且在盒子还没掉下来之前无从验证收益。

### D8 · 加第三种模式 Simulate —— 已定（用户提的）

`EditorContext::Mode` 从 `{ Edit, Play }` 变成 `{ Edit, Simulate, Play }`。三者的区别只有两条轴：

| | 跑 `World::tick()` | 相机 | gizmo | 调试线（选中轮廓 / 光源线框） |
| --- | --- | --- | --- | --- |
| **Edit** | 否 | 编辑器相机 | 开 | 画 |
| **Simulate** | **是** | **编辑器相机** | **开** | **画** |
| **Play** | 是 | 场景的 primary 相机 | 关 | 不画 |

也就是说 **Simulate = 系统在跑，但你还坐在编辑器里**：可以自由飞、可以点选正在下落的盒子、
可以在 Inspector 里看它的值变化、可以用 gizmo 把它推一把。Play 是"以游戏的方式看"。

这正是 Unreal 的 Play / Simulate 之分。对物理这件事它不是可选项 —— 调物理参数时你要的恰恰是
"一边跑一边从任意角度看、一边选中看数值"，而 Play 模式把相机交给了场景相机、还关掉了 gizmo。

**快照沿用现有机制**：进 Simulate 和进 Play 都拷快照，退出都拷回。所以"模拟一下再撤销"是免费的，
这也是 Simulate 有用的另一半原因。

**只允许 Edit ↔ Simulate 和 Edit ↔ Play**，不做 Simulate ↔ Play 的直接切换（UE 允许，但那要处理
"切过去之后快照算谁的"）。工具栏两个按钮，激活的那个变成 Stop。

**要动的调用点**（`isPlaying()` 现在有五处，每一处都要重新判断该按哪条轴）：

| 位置 | 现在 | 应该 |
| --- | --- | --- |
| `editor_layer.cpp:145` 调 `updatePlay` | `isPlaying()` | **跑系统**：`mode != Edit` |
| `:163` gizmo 开关 | `!isPlaying()` | **gizmo**：`mode != Play` |
| `:211` 相机覆盖 | `!isPlaying()` | **相机**：`mode != Play` |
| `:225` 调试线 | `!isPlaying()` | **调试线**：`mode != Play` |
| `:322` 工具栏 | `isPlaying()` | 三态 |
| `scene_document.cpp:46` 换场景前退出 | `exitPlayMode()` | 退出到 Edit（不管当前是哪个） |

**这几处不能笼统地用一个 `isPlaying()` 替换** —— 它们分属两条不同的轴（"跑不跑系统"和
"是不是游戏视角"），现在恰好重合是因为只有两种模式。混用会得到"Simulate 下 gizmo 消失"
或者"Play 下还在画选中轮廓"这类错。所以 `EditorContext` 上要给出两个**语义明确**的查询，
而不是让调用方比较枚举：

```cpp
bool isSimulating() const noexcept;   // mode != Edit，跑系统
bool isGameView() const noexcept;     // mode == Play，场景相机 / 无 gizmo / 无调试线
```

`updatePlay` 顺势改名 `updateSimulation`。

### 待定：无

D1～D8 全部已定。执行时发现某条行不通，**先在交接区记下来再改**，不要默默换方案。

---

## 任务清单

五个阶段。**阶段 3 结束就能看到盒子掉下来**，1、2 都是不改画面的铺垫。

### 阶段 1 · 接入 box3d（不碰引擎集成）

- [x] **1.1 加 submodule 并 pin tag**
  - 命令：`git submodule add -b main https://github.com/erincatto/box3d.git third_party/box3d`
  - 文件：`.gitmodules`（确认 `branch = main` 写进去了）、根 `CMakeLists.txt`
    （加进那个 submodule 存在性检查的 `foreach`）
  - 验收：`git submodule status` 里有 box3d；`.gitmodules` 里有 `branch = main`。
  - 升级的规矩（写进交接区，以后照做）：`git submodule update --remote third_party/box3d`
    → 跑 `physics_smoke` 和端到端验收 → 指针单独一个 `chore(deps)` commit。

- [x] **1.2 挂进构建**
  - 文件：`third_party/CMakeLists.txt`
  - 做法：`add_subdirectory(box3d)` 之前把它自己的开关关掉 —— 至少
    `BOX3D_SAMPLES` / `BOX3D_BENCHMARK` / `BOX3D_UNIT_TESTS` / `BOX3D_DOCS`（**实际名字要看它的
    `CMakeLists.txt`**，别照抄我这里写的）。样例要拉 sokol 和 imgui，一定要关掉。
  - 注意：本项目用**独立 clang 走 MSVC ABI**，而 box3d 是 C17 + 默认开 SSE2/Neon。要确认它作为
    子目录被消费时不会强加自己的编译选项或 CRT 设置。真撞上了，`BOX3D_DISABLE_SIMD` 是它提供的
    退路（先别用，SIMD 是它的性能来源）。
  - 验收：`cmake --preset debug` 配置通过，`box3d` target 出现在构建里。

- [x] **1.3 冒烟测试：让一个盒子在纯 C 的世界里掉下来**
  - 文件：`ArtiEngine/runtime/tests/physics_smoke.cpp`（新建）+ 挂进 `BUILD_TESTING` 分支，
    照 `Tools/asset_tools/CMakeLists.txt` 里 `asset_pipeline_smoke` 的形状写
  - 做法：照 Box3D 的 hello 文档走一遍 ——
    ```c
    b3WorldDef wd = b3DefaultWorldDef();        // gravity 默认 (0,-10,0)
    b3WorldId world = b3CreateWorld(&wd);
    b3BodyDef gd = b3DefaultBodyDef();          // 默认就是静态
    gd.position = (b3Vec3){0, -10, 0};
    b3BodyId ground = b3CreateBody(world, &gd);
    b3BoxHull gbox = b3MakeBoxHull(50, 10, 50); // **半长**，所以上表面在 y=0
    b3ShapeDef gsd = b3DefaultShapeDef();
    b3CreateHullShape(ground, &gsd, &gbox.base);

    b3BodyDef bd = b3DefaultBodyDef();
    bd.type = b3_dynamicBody;                   // **不设就不会动**
    bd.position = (b3Vec3){0, 4, 0};
    b3BodyId box = b3CreateBody(world, &bd);
    b3BoxHull cube = b3MakeCubeHull(1.0f);
    b3ShapeDef sd = b3DefaultShapeDef();
    sd.density = 1.0f;                          // dynamic body 至少要一个非零密度的 shape
    b3CreateHullShape(box, &sd, &cube.base);

    for (int i = 0; i < 90; ++i) b3World_Step(world, 1.0f/60.0f, 4);
    b3Vec3 p = b3Body_GetPosition(box);         // 应该稳定在 y≈1
    b3DestroyWorld(world);
    ```
  - 验收：`ctest` 里这个测试过 —— 断言 90 步之后 `p.y` 落在 `[0.9, 1.1]`。
  - 这一步的意义：**在碰引擎之前先确认库本身是通的**。后面出问题就不用怀疑到这一层。
  - 顺手确认：**球和胶囊的创建函数叫什么**。我只从文档确认了 `b3CreateHullShape`，
    文档说「每种几何有自己的创建调用」但没列全。看头文件，把实际名字记进交接区。

### 阶段 2 · 组件与序列化（画面还不变）

- [x] **2.1 两个组件**
  - 文件：`ArtiEngine/scene/components.h`
  - 做法：按 D4 加 `RigidBodyComponent` 和 `ColliderComponent`。默认值：
    body 是 `Dynamic`、`gravity_scale = 1`、`enable_sleep = true`；
    collider 是 `Box`、半长 `(0.5, 0.5, 0.5)`（正好一个单位立方体）、
    `density = 1`、`friction = 0.3`（抄 Box3D hello 里的值）、`restitution = 0`。
  - 注意：尺寸存**半长**还是全长？Box3D 的 `b3MakeBoxHull` 吃半长。组件里也存半长、
    并在字段名和 Inspector 标签里写明「Half Extents」—— 两边一致，省掉一次转换和一类 bug。

- [x] **2.2 注册与序列化**
  - 文件：`ArtiEngine/scene/component_registration.cpp`（两个 `registerComponentCopy<>`，
    否则 Play 快照会丢掉它们）、`scene/component_serialization.{h,cpp}`（两个稳定的
    `typeName()`：`artiengine.rigid_body` / `artiengine.collider`，写无条件、读容忍缺失）
  - 验收：加了组件的场景存盘后重新打开，值还在；老场景（没有这两个组件）照常加载。

- [x] **2.3 Inspector**
  - 文件：`Tools/scene_editor/src/panels/inspector_panel.cpp`（+ 头文件里两个 `draw...` 声明）
  - 做法：body 一个类型下拉 + 两个字段；collider 一个形状下拉 + 尺寸 + 三个材质字段。
    形状切换时只显示该形状用到的尺寸字段（盒子三个半长、球一个半径、胶囊半径 + 半高）。
  - 验收：能在编辑器里给一个立方体加上这两个组件并调参数、存盘、重开还在。

### 阶段 3 · 物理系统（`FixedUpdate` 的第一个消费者）

- [x] **3.1 `PhysicsSystem` 骨架**
  - 文件：`ArtiEngine/runtime/physics_system.{h,cpp}`（新建）+ 挂进 `ArtiEngine/CMakeLists.txt` 的
    `artiengine_runtime` 源文件列表；`artiengine_runtime` 链 `box3d`
  - 做法：继承 `scene::SceneSystem`，实现 `onUpdate(Scene&, const UpdateContext&)`。
    Box3D 的世界句柄和「实体 ↔ body」的双向表放在 `Impl` 里（pimpl，别让 `b3*` 类型出现在头里 ——
    头会被 `World` 包含，而 `World` 的头被编辑器和 player 都包含）。
  - 验收：编译通过。

- [x] **3.2 建世界 / 拆世界**
  - 做法：`frameIndex == 0` 时（见 D5）拆掉旧世界、按当前场景重建：遍历同时有
    `RigidBodyComponent` 和 `ColliderComponent` 的实体，跳过有父级的（warn）、跳过非单位缩放的
    （warn），按组件建 body 和 shape，`bodyDef.userData` 存实体的 UUID。
  - 注意：`userData` 是 `void*`，而 UUID 是 64 位值 —— 可以直接塞进指针位宽（先 `static_assert`
    确认 `sizeof(void*) >= sizeof(UUID::Value)`），或者存一个索引进自己的数组。**塞指针位宽更省**，
    但要在注释里写明这是有意的。
  - 验收：临时日志打出「建了 N 个 body」，数目和场景里的物理实体数对得上。**验完删日志。**

- [x] **3.3 step 与写回**
  - 做法：`b3World_Step(world, context.fixedDeltaTime, 4)`，然后
    `b3World_GetBodyEvents()` 遍历移动事件，按 `userData` 找实体，写 `TransformComponent` 的
    translation 和 rotation（**不动 scale**）。
  - 注意：`b3Body_GetRotation()` 返回的四元数是 `.v`（xyz）+ `.s`（标量）两段，而 `glm::quat`
    的构造是 `(w, x, y, z)` —— **这里最容易把 w 放错位置**，而放错的表现是物体绕着奇怪的轴转，
    不是不动。写的时候明确写成 `glm::quat{ r.s, r.v.x, r.v.y, r.v.z }`。
  - 验收：**盒子掉下来、落在地面上、停住**。放三个盒子能堆起来。
    验法：`PrintWindow` 抓播放器窗口（见交接区的验证手段），Play 前后各抓一张。

### 阶段 4 · Simulate 模式 + 编辑器体验

这一阶段放在物理能跑**之后**，因为 Simulate 只有在有东西会动的时候才验得出来。

- [x] **4.1 `EditorContext` 三态化**
  - 文件：`Tools/scene_editor/src/editor_context.{h,cpp}`
  - 做法：`Mode` 加 `Simulate`；`isPlaying()` 换成**两个语义明确的查询**
    `isSimulating()`（`mode != Edit`）和 `isGameView()`（`mode == Play`）；
    `enterPlayMode()` / `exitPlayMode()` 变成 `enterMode(Mode)` / `exitToEdit()`；
    `updatePlay()` 改名 `updateSimulation()`。
  - 注意：**别保留 `isPlaying()`** —— 留着它，下一个人会继续用，而它的语义正是这次要拆开的那个
    含混点。让编译器把五个调用点全报出来。
  - 验收：编译报错列出所有调用点（这是预期的），逐个按 D8 的表改完之后编译通过。

- [x] **4.2 工具栏三态**
  - 文件：`Tools/scene_editor/src/editor_layer.cpp` 的 `drawToolbar()`
  - 做法：两个按钮 `Play` / `Simulate`，激活的那个显示成 `Stop`，另一个禁用
    （D8：不做直接切换）。状态文字 `[Edit]` / `[Simulate]` / `[Play]`。
    gizmo 的那几个操作按钮的显示条件从 `!playing` 改成 `!isGameView()`。
  - 验收：三种状态下工具栏显示正确，按钮不会出现"两个都能按"的状态。

- [x] **4.3 五个调用点按两条轴改对**
  - 文件：`editor_layer.cpp`（五处）、`scene_document.cpp`（一处）
  - 做法：照 D8 那张表。`scene_document.cpp` 那处换场景前无条件退到 Edit。
  - 验收：**Simulate 下 gizmo 还在、能自由飞、能点选、能看到选中轮廓和光源线框，
    而盒子在掉**；Play 下这些都没有、用的是场景相机。
  - 这条是本阶段的核心验收 —— 两条轴混淆的表现正好是它的反面。

- [x] **4.4 Stop 之后回到原位（验证，不是实现）**
  - 做法：不该需要写代码 —— `EditorContext` 的快照机制应该已经覆盖。
  - 验收：Simulate → 盒子掉下来 → Stop → 回到原位；Play 同样。
  - 如果没回去：说明 `TransformComponent` 没被 `registerComponentCopy` 覆盖（它是内置五个之一，
    应该有），或者快照时机不对。先在交接区记下来再改。

- [x] **4.5 两条 warn 真的会出现**
  - 验收：给一个有父级的实体加物理组件 → 日志一条 warn 且它不参与模拟；
    缩放不是 1 的实体 → 同样有 warn。
  - 注意：warn 只在建世界时打一次，**不要每帧打** —— 那会淹掉日志。

### 阶段 5 · 收尾

- [x] **5.1 文档**
  - 文件：`docs/Architecture/Scene.md`（组件表加两个新组件、说明 `FixedUpdate` 现在有消费者了 ——
    那一节现在写的是「ArtiEngine 目前没有注册任何系统」，要改）、
    `docs/Architecture/README.md`（分层表里 Runtime 那行提一句物理；target 依赖表加 box3d；
    「明确未做」里「物理 / 脚本 / 音频」那条拆开 —— 物理有了，剩下脚本和音频，
    并新增「物理的射线查询 / 触发器 / 关节 / 多线程」和「带父级或有缩放的实体不参与模拟」）、
    `docs/Architecture/Applications.md`（编辑器那节的 Edit / Play 两模式描述要改成三模式，
    并说清「跑不跑系统」和「是不是游戏视角」是两条独立的轴）
  - 验收：`grep -rn "没有注册任何系统\|完全没有" docs/Architecture/` 没有过期残留。

- [x] **5.2 提交**
  - 只有 ArtiEngine 一层（含 submodule 指针）。ArtiRenderer / ArtiChoco **不该动** ——
    动了说明有个设计决定变了（最可能是 D5 那个将就撑不住了），先记进交接区。

---

## 端到端验收

1. `ctest` 里 `physics_smoke` 过（纯 C，不涉及引擎）。
2. 编辑器里给一个立方体加 `RigidBody`（Dynamic）+ `Collider`（Box），把它放在地面上方。
3. 给地面那块加 `RigidBody`（Static）+ `Collider`（Box）。
4. **Simulate → 盒子掉下来、落在地面上、停住**，而且这期间：
   - 相机还是编辑器相机，能自由飞
   - 能点选正在下落的盒子，Inspector 里的 Transform 数值在变
   - 选中轮廓和光源线框照常画
   - gizmo 还在（能把盒子推一把）
5. **Stop → 所有盒子回到原来的位置和朝向。**
6. **Play → 同样会掉**，但相机换成场景的 primary 相机、gizmo 消失、调试线不画。
7. 放三个盒子叠着 → 能堆起来不抖。
8. 存盘、重开项目 → 组件和参数都还在。
9. `arti_player` 跑同一个项目 → 表现和编辑器 Play 一致（验的是 D2 那「只有一处注册」）。
10. 给一个有父级的实体加物理组件 → 日志里一条 warn，它不参与模拟，其余照常。
11. 工具栏：三种状态显示正确，不会出现「Play 和 Simulate 两个都能按」。

## 风险与注意

### Box3D 是 alpha

API 会变，手册还在写（头文件有完整 Doxygen，成文手册未完成），**PR 目前关闭**（只能提 issue）。
所以：submodule **跟 main**（D1，用户拍的），但升级是个**显式动作**：
`git submodule update --remote` → 过 `physics_smoke` 和端到端验收 → 指针单独一个 commit。
普通的 `git submodule update` 不会把我们带到新版本上，所以日常开发不会被上游的变动撞到。

我这份文档里的 API 名字是从它的 hello 文档抄的，**已确认**的有：
`b3DefaultWorldDef` / `b3CreateWorld` / `b3DestroyWorld` / `b3DefaultBodyDef` / `b3CreateBody` /
`b3_dynamicBody` / `b3MakeBoxHull`（半长）/ `b3MakeCubeHull` / `b3DefaultShapeDef` /
`b3CreateHullShape(bodyId, &shapeDef, &hull.base)` / `b3World_Step(world, dt, subSteps)` /
`b3Body_GetPosition` / `b3Body_GetRotation` / `b3World_GetBodyEvents` / `b3Body_SetTargetTransform` /
`b3Body_SetTransform` / `b3Vec3` / `b3Quat` / `b3WorldId` / `b3BodyId`。

**没确认**的：球和胶囊的创建函数名、`b3World_GetBodyEvents` 返回结构的字段名、
CMake 选项的实际名字。这三样阶段 1 看头文件时确认，记进交接区。

### 四元数的分量顺序

`b3Body_GetRotation()` 返回 `.v`（xyz 向量部分）+ `.s`（标量部分）两段；`glm::quat` 的构造是
`(w, x, y, z)`。**把 w 放错位置的表现是物体绕着奇怪的轴转，而不是不动** —— 看起来像"物理不对"
而不是"代码写错"。明确写成 `glm::quat{ r.s, r.v.x, r.v.y, r.v.z }`。

（序列化那边也是 `[w, x, y, z]`，见 `transform_serialization.h` 的 `writeQuaternion`。
这个项目里四元数一律 w 在前。）

### 不要用 `b3Body_SetTransform` 做持续运动

它的文档明确说这被当成**传送**，会导致"undesirable behavior and/or performance problems"。
驱动运动（动画、平台）要用 kinematic body + `b3Body_SetTargetTransform`，它会解出一步内到位所需
的速度。v1 不做驱动运动，但**将来做动画时会撞上这条**，先记着。

### 移动事件是瞬时的

`b3World_GetBodyEvents()` 的数据只在 step 之后、下一次 step 之前有效。要在 `onUpdate` 里当场
消费完，别存指针。它的文档还提醒 end-touch 事件可能引用已销毁的对象，要用
`b3Shape_IsValid` / `b3Joint_IsValid` 校验 —— v1 不用触发器，暂时碰不到。

### `FixedUpdate` 第一次真跑

这条路从来没被走过。可能撞上的：
- `runSystems(FixedUpdate)` 在一帧里可能被调**多次**（追帧），物理要能承受 —— 它本来就是为此设计的，
  但**建世界那段绝对不能放在会被调多次的位置**（`frameIndex == 0` 的判断要配一个"这一帧已经建过了"
  的保护，否则追帧的第一帧会建好几次）。
- `max_frame_time = 0.25` 意味着掉帧严重时最多补 15 步。够了，但别指望它能追上断点调试造成的几秒空档。
- 世界变换的更新时机：`updateWorldTransforms()` 在每次 `runSystems()` **之前**跑。物理写的是
  `TransformComponent`，所以它的结果要到下一次 `updateWorldTransforms()` 才反映到
  `WorldTransformComponent` —— 而抽取器每帧自己会调一次，所以渲染看到的是最新的。这条链是通的，
  但**如果将来在 `LateUpdate` 里读世界变换，要记得它是这一帧物理之前的值**。

### 不在本任务范围内

- 射线查询、触发器（sensor）、关节 —— D6
- 多线程（桥到 enkiTS）—— D7
- 三角网格 / 高度场碰撞体（静态关卡几何用得上）
- 复合体（一个 body 多个 collider）
- 带父级的实体、非单位缩放的实体 —— D3，会 warn 并跳过
- 角色控制器（Box3D 有 character mover，但那是另一件事）
- 渲染插值（`FixedTimestepAccumulator::alpha()` 是现成接缝，v1 不用）
- Simulate ↔ Play 的直接切换（D8）—— 得先回 Edit

---

## 参考

- [Announcing Box3D](https://box2d.org/posts/2026/06/announcing-box3d/) —— 来历、feature 列表、
  alpha 状态、四个在用的项目
- [Box3D 文档总览](https://box2d.org/documentation3d/) 与
  [Simulation](https://box2d.org/documentation3d/md_simulation.html) —— body 类型、shape、
  子步、固定步长建议、休眠、**以及「怎么和引擎的 transform 系统集成」那一节（`GetBodyEvents`）**
- [Box3D Hello World](https://github.com/erincatto/box3d/blob/main/docs/hello.md) —— 阶段 1.3 照它写
- [Box3D README](https://github.com/erincatto/box3d/blob/main/README.md) —— MIT、C17、无依赖、
  CMake preset、形状与关节列表、SIMD 开关
- [物理引擎对比（Rapier / Box2D / Box3D / Jolt）](https://forum.godotengine.org/t/physics-engine-comparison-rapier-vs-godot-vs-box2d-3d-vs-jolt/142786)
  —— 上面那组数字的出处。**注意是 godot-rapier 维护者跑的**，harness 公开但立场要打折
- [Jolt Physics](https://github.com/jrouwe/JoltPhysics) —— 没选它，但它是那条路的参照
