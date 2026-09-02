# 物理：接入 Box3D，让东西掉下来

| | |
| --- | --- |
| **状态** | 未开始 |
| **创建** | 2026-09-02 |
| **最后更新** | 2026-09-02 |
| **涉及仓库** | ArtiEngine（全部改动都在这里；ArtiRenderer / ArtiChoco 不动） |
| **目标** | 刚体物理：场景里放一个盒子，Play 之后它掉下来、落在地面上、能堆叠。`FixedUpdate` 这个 stage 第一次真正跑起来 |

---

## 交接区

> **全文唯一允许改动的段落。每次收工前更新这里。**

**当前进度**：未开始。阶段 1～5 全部未动。

**下一步**：阶段 1.1，把 box3d 作为 submodule 接进来并 pin 在 `v0.1.0`。这一步不碰引擎集成，
只确认「库能建起来、能让一个盒子在纯 C 的冒烟测试里掉下来」。

**决定记录**（时间倒序，新的加在最上面）：

- 2026-09-02 用户拍板：**用 Box3D**（不是 Jolt），其余按我的建议 —— transform 在模拟期归物理、
  第一版只做球 / 盒 / 胶囊、不做射线查询和触发器。

**踩到的坑**：（暂无）

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

### D1 · box3d 走 submodule，pin 在 `v0.1.0` —— 已定

放在 `ArtiEngine/third_party/box3d`，和 ImGuizmo 一样在 `third_party/CMakeLists.txt` 里
`add_subdirectory` 进来，并加进根 `CMakeLists.txt` 那个「少一层 submodule 就在配置期直接报」的
检查列表。

**必须 pin tag 而不是跟 main**：它是 alpha，API 会变。跟 main 的话某天 `git submodule update`
之后编译不过，而那时你正在查别的问题。升级版本应该是一个显式的、单独的 commit。

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

### 待定：无

D1～D7 全部已定。执行时发现某条行不通，**先在交接区记下来再改**，不要默默换方案。

---

## 任务清单

五个阶段。**阶段 3 结束就能看到盒子掉下来**，1、2 都是不改画面的铺垫。

### 阶段 1 · 接入 box3d（不碰引擎集成）

- [ ] **1.1 加 submodule 并 pin tag**
  - 命令：`git submodule add https://github.com/erincatto/box3d.git third_party/box3d`
    然后在 `third_party/box3d` 里 `git checkout v0.1.0`，回到根目录 `git add third_party/box3d`
  - 文件：`.gitmodules`、根 `CMakeLists.txt`（加进那个 submodule 存在性检查的 `foreach`）
  - 验收：`git submodule status` 里有 box3d 且指向 v0.1.0 的 commit。

- [ ] **1.2 挂进构建**
  - 文件：`third_party/CMakeLists.txt`
  - 做法：`add_subdirectory(box3d)` 之前把它自己的开关关掉 —— 至少
    `BOX3D_SAMPLES` / `BOX3D_BENCHMARK` / `BOX3D_UNIT_TESTS` / `BOX3D_DOCS`（**实际名字要看它的
    `CMakeLists.txt`**，别照抄我这里写的）。样例要拉 sokol 和 imgui，一定要关掉。
  - 注意：本项目用**独立 clang 走 MSVC ABI**，而 box3d 是 C17 + 默认开 SSE2/Neon。要确认它作为
    子目录被消费时不会强加自己的编译选项或 CRT 设置。真撞上了，`BOX3D_DISABLE_SIMD` 是它提供的
    退路（先别用，SIMD 是它的性能来源）。
  - 验收：`cmake --preset debug` 配置通过，`box3d` target 出现在构建里。

- [ ] **1.3 冒烟测试：让一个盒子在纯 C 的世界里掉下来**
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

- [ ] **2.1 两个组件**
  - 文件：`ArtiEngine/scene/components.h`
  - 做法：按 D4 加 `RigidBodyComponent` 和 `ColliderComponent`。默认值：
    body 是 `Dynamic`、`gravity_scale = 1`、`enable_sleep = true`；
    collider 是 `Box`、半长 `(0.5, 0.5, 0.5)`（正好一个单位立方体）、
    `density = 1`、`friction = 0.3`（抄 Box3D hello 里的值）、`restitution = 0`。
  - 注意：尺寸存**半长**还是全长？Box3D 的 `b3MakeBoxHull` 吃半长。组件里也存半长、
    并在字段名和 Inspector 标签里写明「Half Extents」—— 两边一致，省掉一次转换和一类 bug。

- [ ] **2.2 注册与序列化**
  - 文件：`ArtiEngine/scene/component_registration.cpp`（两个 `registerComponentCopy<>`，
    否则 Play 快照会丢掉它们）、`scene/component_serialization.{h,cpp}`（两个稳定的
    `typeName()`：`artiengine.rigid_body` / `artiengine.collider`，写无条件、读容忍缺失）
  - 验收：加了组件的场景存盘后重新打开，值还在；老场景（没有这两个组件）照常加载。

- [ ] **2.3 Inspector**
  - 文件：`Tools/scene_editor/src/panels/inspector_panel.cpp`（+ 头文件里两个 `draw...` 声明）
  - 做法：body 一个类型下拉 + 两个字段；collider 一个形状下拉 + 尺寸 + 三个材质字段。
    形状切换时只显示该形状用到的尺寸字段（盒子三个半长、球一个半径、胶囊半径 + 半高）。
  - 验收：能在编辑器里给一个立方体加上这两个组件并调参数、存盘、重开还在。

### 阶段 3 · 物理系统（`FixedUpdate` 的第一个消费者）

- [ ] **3.1 `PhysicsSystem` 骨架**
  - 文件：`ArtiEngine/runtime/physics_system.{h,cpp}`（新建）+ 挂进 `ArtiEngine/CMakeLists.txt` 的
    `artiengine_runtime` 源文件列表；`artiengine_runtime` 链 `box3d`
  - 做法：继承 `scene::SceneSystem`，实现 `onUpdate(Scene&, const UpdateContext&)`。
    Box3D 的世界句柄和「实体 ↔ body」的双向表放在 `Impl` 里（pimpl，别让 `b3*` 类型出现在头里 ——
    头会被 `World` 包含，而 `World` 的头被编辑器和 player 都包含）。
  - 验收：编译通过。

- [ ] **3.2 建世界 / 拆世界**
  - 做法：`frameIndex == 0` 时（见 D5）拆掉旧世界、按当前场景重建：遍历同时有
    `RigidBodyComponent` 和 `ColliderComponent` 的实体，跳过有父级的（warn）、跳过非单位缩放的
    （warn），按组件建 body 和 shape，`bodyDef.userData` 存实体的 UUID。
  - 注意：`userData` 是 `void*`，而 UUID 是 64 位值 —— 可以直接塞进指针位宽（先 `static_assert`
    确认 `sizeof(void*) >= sizeof(UUID::Value)`），或者存一个索引进自己的数组。**塞指针位宽更省**，
    但要在注释里写明这是有意的。
  - 验收：临时日志打出「建了 N 个 body」，数目和场景里的物理实体数对得上。**验完删日志。**

- [ ] **3.3 step 与写回**
  - 做法：`b3World_Step(world, context.fixedDeltaTime, 4)`，然后
    `b3World_GetBodyEvents()` 遍历移动事件，按 `userData` 找实体，写 `TransformComponent` 的
    translation 和 rotation（**不动 scale**）。
  - 注意：`b3Body_GetRotation()` 返回的四元数是 `.v`（xyz）+ `.s`（标量）两段，而 `glm::quat`
    的构造是 `(w, x, y, z)` —— **这里最容易把 w 放错位置**，而放错的表现是物体绕着奇怪的轴转，
    不是不动。写的时候明确写成 `glm::quat{ r.s, r.v.x, r.v.y, r.v.z }`。
  - 验收：**盒子掉下来、落在地面上、停住**。放三个盒子能堆起来。
    验法：`PrintWindow` 抓播放器窗口（见交接区的验证手段），Play 前后各抓一张。

### 阶段 4 · 编辑器体验

- [ ] **4.1 Stop 之后回到原位**
  - 做法：不用写代码 —— `EditorContext` 的快照机制应该已经覆盖了（进 Play 拷快照、Stop 拷回）。
    这一步是**验证**而不是实现。
  - 验收：Play → 盒子掉下来 → Stop → 盒子回到原来的位置和朝向。
  - 如果没回去：说明 `TransformComponent` 没被 `registerComponentCopy` 覆盖（它是内置的五个之一，
    应该有），或者快照的时机不对。先在交接区记下来再改。

- [ ] **4.2 两条 warn 真的会出现**
  - 验收：给一个有父级的实体加物理组件 → 日志里有 warn 且它不参与模拟；
    给一个缩放不是 1 的实体加 → 同样有 warn。
  - 注意：warn 只在建世界时打一次，**不要每帧打** —— 那会淹掉日志。

- [ ] **4.3 Play 中拖 gizmo**
  - 做法：Play 模式下 gizmo 已经是禁用的（`editor_layer.cpp` 里
    `const bool gizmo_enabled = !m_context->isPlaying();`），所以这一步大概也是**验证**。
  - 验收：Play 中 gizmo 不出现，不会出现「拖了但物理立刻拽回去」的拉锯。

### 阶段 5 · 收尾

- [ ] **5.1 文档**
  - 文件：`docs/Architecture/Scene.md`（组件表加两个新组件、说明 `FixedUpdate` 现在有消费者了 ——
    那一节现在写的是「ArtiEngine 目前没有注册任何系统」，要改）、
    `docs/Architecture/README.md`（分层表里 Runtime 那行提一句物理；target 依赖表加 box3d；
    「明确未做」里「物理 / 脚本 / 音频」那条拆开 —— 物理有了，剩下脚本和音频，
    并新增「物理的射线查询 / 触发器 / 关节 / 多线程」和「带父级或有缩放的实体不参与模拟」）
  - 验收：`grep -rn "没有注册任何系统\|完全没有" docs/Architecture/` 没有过期残留。

- [ ] **5.2 提交**
  - 只有 ArtiEngine 一层（含 submodule 指针）。ArtiRenderer / ArtiChoco **不该动** ——
    动了说明有个设计决定变了（最可能是 D5 那个将就撑不住了），先记进交接区。

---

## 端到端验收

1. `ctest` 里 `physics_smoke` 过（纯 C，不涉及引擎）。
2. 编辑器里给一个立方体加 `RigidBody`（Dynamic）+ `Collider`（Box），把它放在地面上方。
3. 给地面那块加 `RigidBody`（Static）+ `Collider`（Box）。
4. **Play → 盒子掉下来、落在地面上、停住。**
5. 放三个盒子叠着 → 能堆起来不抖。
6. **Stop → 所有盒子回到原来的位置和朝向。**
7. 存盘、重开项目 → 组件和参数都还在。
8. `arti_player` 跑同一个项目 → 表现和编辑器 Play 一致（验的是 D2 那「只有一处注册」）。
9. 给一个有父级的实体加物理组件 → 日志里一条 warn，它不参与模拟，其余照常。

---

## 风险与注意

### Box3D 是 alpha

API 会变，手册还在写（头文件有完整 Doxygen，成文手册未完成），**PR 目前关闭**（只能提 issue）。
所以：submodule **pin tag**，升级版本单独一个 commit，而且升级时要过一遍 `physics_smoke`。

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
