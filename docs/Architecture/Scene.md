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

系统分四个 stage：`FixedUpdate` / `Update` / `LateUpdate` / `RenderExtract`。ArtiEngine 目前
**没有注册任何系统** —— 抽取是 `SceneRenderer` 直接调的，不走 `RenderExtract`。四个 stage
都空着但循环在跑，是物理 / 脚本将来的挂载点。

## 2. ArtiEngine 的六个组件

`ArtiEngine/scene/components.h`。都是纯数据。

| 组件 | 序列化名 | 字段 |
| --- | --- | --- |
| `MeshRendererComponent` | `artiengine.mesh_renderer` | mesh handle、每个 submesh 一个材质 handle、`visible` |
| `CameraComponent` | `artiengine.camera` | `fov_degrees` / `near_plane` / `far_plane` / `primary` |
| `DirectionalLightComponent` | `artiengine.directional_light` | `color` / `intensity` / `enabled` / `casts_shadow`（默认 true）/ `shadow_distance`（默认 100） |
| `PointLightComponent` | `artiengine.point_light` | `color` / `intensity`（默认 25）/ `range` / `enabled` |
| `SpotLightComponent` | `artiengine.spot_light` | 同上 + `inner_cone_degrees` / `outer_cone_degrees` |
| `EnvironmentComponent` | `artiengine.environment` | equirect 贴图 handle、`sky_color` / `intensity` / `enabled` / `sky_visible` |

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

### 注册

`registerSceneComponents()`（`ArtiEngine/scene/component_registration.h`）一次做两件事：

1. `Scene::registerComponentCopy<T>()` —— 六个组件都注册，让 Play 模式的场景快照能完整拷贝。
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

- **没有视锥剔除**。`world_bounds` 每帧都算好了、`Renderer::meshInfo()` 也能拿到局部包围盒，
  但没有人拿它们做剔除，`FrameStatistics::culled` 恒为 0。
- **没有排序 / 批合并**。`draws` 就是遍历顺序。
- **`MeshInfo` 按 `MeshHandle` 缓存在抽取器里**，避免每帧对每个实体查一次渲染器。

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
   Play 模式的快照会丢掉它（会记一条 warn，不会崩）。
3. `ArtiEngine/scene/component_serialization.{h,cpp}` —— 实现 `ComponentSerialization<T>`，
   给一个稳定的 `typeName()`，在 `registerSceneSerialization()` 里登记。
4. 要影响画面的话，`ArtiEngine/scene/render_scene_extractor.cpp` 里加一遍 view 扫描。
5. 要能在 Inspector 里编辑的话，`Tools/scene_editor/src/panels/inspector_panel.cpp`。

克隆注册和序列化注册**刻意分开**：有些运行时组件可拷贝但不该持久化。
