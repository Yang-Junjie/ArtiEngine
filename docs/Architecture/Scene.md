# 场景

> 本页讲 **ArtiEngine 在 ECS 之上填了什么**：有哪些游戏组件、怎么序列化、`World` 怎么 tick、
> 怎么抽成渲染数据。ECS 本身的 API 与不变式（实体生命周期、层级、世界变换、系统 stage、
> 克隆、YAML 序列化框架）在 `ArtiRenderer/ArtiChoco/artichoco/scene/README.md`。

## 1. 基座：ArtiChoco::Scene

EnTT 之上的 ECS，`Scene` 是门面和聚合根。每个实体自带五个必需组件：

| 组件 | 含义 | 可变性 |
| --- | --- | --- |
| `IDComponent` | 持久 UUID，查找和序列化的主键 | 场景所有，只读 |
| `TagComponent` | 人可读的名字 | 可变 |
| `TransformComponent` | 局部 translation / rotation / scale | 可变 |
| `ParentComponent` | 父实体 UUID | 场景所有，只读 |
| `WorldTransformComponent` | 缓存的派生世界变换 | 场景所有，只读 |

层级只能通过 `Scene::setParent()` 改 —— 它会校验场景归属、拒绝自环和成环。世界变换在每次
`runSystems()` 之前更新，只重算局部变换 / 父级 / 脏标记变过的分支。

`Scene::duplicateEntity(entity)` 复制一个实体**连同它整棵子树**，返回新的根：每个副本拿一个
新 UUID（身份归场景所有，不跟着拷）；新根的父级和源一致，所以副本是源的**兄弟**；子树内部的
父子关系重映射到副本上。重映射靠的是 `ParentComponent` 存的是 UUID 而不是句柄 —— 查得到
就换、查不到的只有子树的根（它的父级在子树外），所以根不需要特例。**名字原样照抄**，
`Cube → Cube (1)` 那种消歧是编辑器的策略，不在场景语义里。

系统分四个 stage：`FixedUpdate` / `Update` / `LateUpdate` / `RenderExtract`。ArtiEngine 目前
注册了**一个**系统：`PhysicsSystem` 挂在 `FixedUpdate`（见 3.1）。`Update` / `LateUpdate` 还空着
但循环在跑，是脚本将来的挂载点；抽取是 `SceneRenderer` 直接调的，不走 `RenderExtract`。

## 2. ArtiEngine 的八个组件

`ArtiEngine/scene/components.h`。都是纯数据。

| 组件 | 序列化名 | 字段 |
| --- | --- | --- |
| `MeshRendererComponent` | `artiengine.mesh_renderer` | mesh handle、每个 submesh 一个材质 handle、`visible` |
| `CameraComponent` | `artiengine.camera` | `fov_degrees` / `near_plane` / `far_plane` / `primary` |
| `DirectionalLightComponent` | `artiengine.directional_light` | `color` / `intensity` / `enabled` / `casts_shadow`（默认 true）/ `shadow_distance`（默认 100） |
| `PointLightComponent` | `artiengine.point_light` | `color` / `intensity`（默认 25）/ `range` / `enabled` |
| `SpotLightComponent` | `artiengine.spot_light` | 同上 + `inner_cone_degrees` / `outer_cone_degrees` |
| `EnvironmentComponent` | `artiengine.environment` | equirect 贴图 handle、`sky_color` / `intensity` / `enabled` / `sky_visible` |
| `RigidBodyComponent` | `artiengine.rigid_body` | `type`（Static / Kinematic / Dynamic，默认 Dynamic）/ `gravity_scale` / `enable_sleep` |
| `ColliderComponent` | `artiengine.collider` | `shape`（Box / Sphere / Capsule）+ `half_extents` / `radius` / `half_height` + `density` / `friction` / `restitution` |

几个刻意的取舍：

- **光源没有 position / direction 字段**，位置和朝向都从 `WorldTransformComponent` 取，朝向
  约定是 **-Z**。三种光源在这一点上一致。
- **点光源和聚光灯的 `intensity` 默认 25，方向光是 1**。点光源带 1/d² 衰减，intensity 1 在
  5 个单位远处只剩 0.04，加进场景会像是没生效。25 让 5 个单位处大致等效于 intensity 1 的
  方向光。这个数不是光度学单位，只是个纯倍数 —— 真要 lumen / candela 那套得连相机曝光一起改。
- **锥角存角度而不是弧度**：面板上直接编辑，序列化出来也读得懂。转弧度在抽取时做一次
  （`rendering::LightDesc` 那边是弧度）。
- **`range` 不是硬截断**，衰减在这个距离上平滑归零。所以它既是「照多远」，也是将来做光源
  剔除时的包围球半径。
- **环境一个场景只取一份**（抽取时遇到第一个 `EnvironmentComponent` 就 break）。「环境」就是
  唯一的那个背景，不像灯光那样是列表。
- **物理要两个组件都在才会被模拟**，缺一个记一条 warn 并跳过。不做「只有 collider 就当静态碰撞
  体」那种隐式创建 —— 隐式的 body 会让「为什么这东西会挡住我」变得不好查。材质属性
  （density / friction / restitution）挂在 collider 上而不是 body 上，和 Box3D 一致：将来做复合体
  时，一个 body 上各部分的材质本来就该各自不同。
- **碰撞体尺寸是显式写在组件里的，不从 `TransformComponent` 的 scale 推**，而且存的是**半长**
  （和 Box3D 的 `b3MakeBoxHull` 一致，省掉一次转换和一类 bug）。代价是缩放过的实体不参与模拟
  （见 3.1）—— 「一块大地面」因此要拆成两个实体：缩放过的立方体做视觉，不缩放的空实体做碰撞体。
- **三种形状的尺寸字段都常驻**，只有 `shape` 选中的那些有意义。这样来回切形状不会丢掉调好的值，
  Inspector 也只画当前形状用得到的那几行。

### 注册

`registerSceneComponents()`（`ArtiEngine/scene/component_registration.h`）一次做两件事：

1. `Scene::registerComponentCopy<T>()` —— 八个组件都注册，让 Play / Simulate 模式的场景快照能
   完整拷贝。
   **这是进程级的**。
2. 传了 registry 的话，再注册 YAML 序列化策略。序列化 registry **不是**进程级的（是个对象），
   所以每个 `World` 自己持一份。

`World` 的构造函数会调它，所以 `World` 一建好就能读写场景，调用方不需要记得先注册什么。
序列化名是持久的文件格式标识 —— C++ 类改名不能改它。

## 3. World：一个正在跑的世界

`ArtiEngine/runtime/world.h`。三样东西打包在一起：场景本身、场景和磁盘之间那条路、
推动它的时钟。

```cpp
scene::Scene& scene();
bool loadScene(path);        // 失败时场景是空的，不留半个读进来的场景假装成功
bool saveScene(path) const;
void clear();                // 清实体 + 归零时钟
void tick(float dt);
void resetClock();           // 进 Play 模式时调 —— 那是一次新会话的开始
uint64_t frameIndex();
```

`tick()` 的顺序：

```
FixedTimestepAccumulator 按固定步长补齐 → runSystems(FixedUpdate) 若干次
                                        → runSystems(Update)      一次
                                        → runSystems(LateUpdate)  一次
```

`World` 的头文件只前向声明 `Scene`，访问器定义在 `.cpp` 里 —— 不把 EnTT 拖给每个包含它的
翻译单元。

**编辑器的 Play 模式和独立 player 驱动的是同一个 `World`。** 分成两份实现的话，「编辑器里
Play 出来的效果」和「exe 跑出来的效果」会各自漂移，而两边单独看都是对的。

换场景时时钟一并归零：换了场景，上一个场景攒下的固定步长余额没有意义。

### 3.1 物理：`FixedUpdate` 唯一的消费者

`ArtiEngine/runtime/physics_system.h`。Box3D（`third_party/box3d`，submodule 跟 `main`）的封装，
注册在 `World` 的构造函数里 —— **全工程只有这一处**，所以编辑器的 Play / Simulate 和独立 player
跑的是同一份，不会出现「编辑器里能掉、exe 里不动」。

`b3*` 的类型一个都不出现在头文件里（pimpl），`artiengine_runtime` 对 `box3d` 是 PRIVATE 链接，
所以下游看不见 box3d 的头。

一个固定步长做三件事：

```
帧号不再单调递增？ → 拆掉旧的 b3World、按当前场景重建（一个物理实体 = 一个 body + 一个 shape）
b3World_Step(fixedDeltaTime, 4 个子步)
b3World_GetBodyEvents() → 按 userData 里的实体 UUID 找到实体，写回 TransformComponent
```

`FixedTimestepAccumulator` 的默认步长是 1/60，正是 Box3D 推荐的 `1/60 + 4 子步`（内部相当于
240 Hz 求解），两边不用互相迁就。事件数组只在下一次 step 之前有效，所以当场消费完、不存指针。

三条要记住的规矩：

- **模拟期间 transform 归物理。** 写回只动 translation 和 rotation，**不动 scale**。所以在
  Simulate 里拖 gizmo 推不动正在模拟的物体 —— 那是有意的：东西怎么动由物理引擎决定。Stop 之后
  场景从快照恢复，编辑期的值一点没丢。
- **只作用于没有父级、且缩放是 1 的实体。** 另外两种情况建世界时各记一条 warn 并跳过（建一次打
  一次，不是每帧）。物理在世界空间算而 `TransformComponent` 是局部的，带父级要拿父级的世界逆
  矩阵反算，那是另一件事。
- **重建世界的信号是帧号回退**，不是某个显式回调。`resetClock()`（`enterMode()` 和 `loadScene()`
  都会调）把 `frameIndex` 归零，物理看到帧号不再单调递增就重建。比较单调性而不是判 `== 0`，
  是因为 `FixedUpdate` 一帧里可能被调多次（追帧）。这是个将就：更干净的做法是给 `SceneSystem`
  加一个 `onSimulationStart()`，但那要改 ArtiChoco。**这个信号哪天变脆，就该去加那个虚函数，
  不要在这儿叠补丁。**

单线程（`workerCount` 默认 1），没接 Box3D 的任务系统。没有射线查询、触发器、关节、三角网格 /
高度场碰撞体、复合体 —— 现在没有脚本，查到了也没人消费。

两处配套的东西：`ArtiEngine/runtime/tests/physics_smoke.cpp`（`ctest` 里的 `physics_smoke`）只链
box3d、不碰引擎 —— submodule 跟 `main`，所以 `git submodule update --remote` 之后第一个报警的应该
是它；`projects/Assets/Scenes/physics_test.artiscene` 是端到端的场景，除了三个会掉的盒子还故意
放了三个「该被跳过」的实体（缩放过的、只有 collider 的、带父级的）。

## 4. 抽取：Scene → RenderScene

`ArtiEngine/scene/render_scene_extractor.h`。引擎和渲染器之间唯一的翻译点。

```
RenderSceneExtractor::extract(scene, gpu_assets, renderer, target) → const RenderScene&
```

每帧的步骤：

1. `scene.updateWorldTransforms()`，然后清空上一帧的 draws / lights / environment / view。
2. **相机** —— 找第一个 `primary` 的 `CameraComponent`。`view` 是世界矩阵的 affine 逆，
   `projection` 用 `glm::perspectiveRH_ZO` —— `_ZO` 后缀显式选了 [0,1] 的深度范围，所以不依赖
   `GLM_FORCE_DEPTH_ZERO_TO_ONE` 这个宏；并且**不**做额外的 `projection[1][1]` 翻转，因为
   NVRHI 的 Vulkan 后端用负 viewport 高度，屏幕约定是 D3D 式的。编辑器相机走的是同一个函数。
   目标宽高任一为 0 就跳过：宽高比会变成 0/0。
3. **灯光** —— 三种组件各扫一遍，位置 / 朝向从世界矩阵取，锥角转弧度，都塞进同一个
   `lights` 数组。
4. **环境** —— 取第一个，equirect 贴图过 `GPUAssetCache` 换成 `TextureHandle`。
5. **绘制项** —— 每个可见的 `MeshRendererComponent` 按 submesh 展开成一条 `DrawItem`：
   mesh handle、submesh 下标、对应槽位的材质 handle、世界矩阵、**世界包围盒**、picking id。
   材质槽不够时 `material` 留空（渲染端回落到默认）。

### picking id

实体 UUID 是 64 位，GPU 的 ID 缓冲装不下，所以抽取器维护一张双向表：
`UUID ↔ uint32_t`，从 1 开始递增（0 保留给「点在空处」）。id 在 extractor 的生命周期内稳定，
拾取结果回来时用 `entityForPickingId()` 反查。

### 目前不做的

- **没有排序 / 批合并**。`draws` 就是遍历顺序。
- **`MeshInfo` 按 `MeshHandle` 缓存在抽取器里**，避免每帧对每个实体查一次渲染器。

视锥剔除不在抽取这一层做 —— 见 [Rendering.md](Rendering.md) 第 9 节。抽取器只负责把
`world_bounds` 填上（`MeshInfo::bounds.transformed(world)`），可见性由 `FrameContext` 在
提交时按相机视锥算一次。

## 5. 场景文件

`.artiscene`，YAML，由 `ArtiChoco` 的 `SceneSerializer` 读写。

反序列化先建一个 staging 场景并校验（必需组件、UUID 唯一、父引用有效、层级无环），
通过之后才重建世界变换并替换目标场景的存储。**所以读失败不会把目标场景改成半成品。**

场景**还不是资产**：没有 handle、没有 artifact，靠项目根相对路径引用
（`ProjectInfo::start_scene` / `last_open_scene`）。打包时 `.artiscene` 原位拷进产物的
`Assets/` 下，那条路径就仍然成立。

## 6. 加一个组件要动哪几处

1. `ArtiEngine/scene/components.h` —— 结构本身。
2. `ArtiEngine/scene/component_registration.cpp` —— `registerComponentCopy<T>()`，否则
   Play 模式的快照会丢掉它（会记一条 warn，不会崩）。**编辑器的 Duplicate 也吃这张注册表**
   （`Scene::duplicateEntity()` 和快照共用同一份），所以漏注册的后果是两处：Play 一进一出
   丢字段、复制出来的实体缺组件。
3. `ArtiEngine/scene/component_serialization.{h,cpp}` —— 实现 `ComponentSerialization<T>`，
   给一个稳定的 `typeName()`，在 `registerSceneSerialization()` 里登记。
4. 要影响画面的话，`ArtiEngine/scene/render_scene_extractor.cpp` 里加一遍 view 扫描。
5. 要能在 Inspector 里编辑的话，`Tools/scene_editor/src/panels/inspector_panel.cpp`。

克隆注册和序列化注册**刻意分开**：有些运行时组件可拷贝但不该持久化。
