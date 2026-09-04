# Lua 脚本：按实体挂、进资产管线、Play 里真跑

| | |
| --- | --- |
| **状态** | 进行中 |
| **创建** | 2026-09-04 |
| **最后更新** | 2026-09-04 |
| **涉及仓库** | **只有 ArtiEngine。** 不动 ArtiRenderer / ArtiChoco。新 submodule 两个，都 pin tag：`third_party/lua`、`third_party/sol2` |
| **目标** | `.lua` 是第五种资产。实体上挂 `ScriptComponent`，Simulate / Play 里跑 `on_create` / `on_update` / `on_destroy`。第一个可玩的里程碑：WASD 推动一个实体 + 一发射线打中地面。编辑器 Play 和 `arti_player` 跑同一份 `ScriptSystem` |
| **明确不做** | 不做 C++ 脚本层 / 热重载 DLL。不做每实例属性表（v1 脚本自己读别的组件）。不做 `require` 跨脚本、协程、`io`/`os`。不做热重导（ContentHash 仍只写不读）。prefab 仍然带不了脚本（`PrefabNode` 没改）。点光阴影、音频、撤销脚本源文件 —— 都不在这 |

---

## 交接区

> **全文唯一允许改动的段落。每次收工前更新这里。**

**当前进度：阶段 0 ~ 6 的代码、测试、文档全部落地，`ctest` 14/14 全绿。只差编辑器里的人工验收。**

下一步：**在编辑器里走一遍端到端验收第 5 / 6 / 8 条**（Play 里 WASD 能不能推动方块、脚本写错
编辑器是否还能点 Stop、Simulate 下是否也跑）。代码侧没有已知的待办。

已经**自动化验证过**的（不需要再人工确认）：
- 脚本能改场景、抛错后被禁用 —— `script_runtime_smoke`，端到端走完整条路（写 `.lua` →
  reconcile → 挂实体 → `World::tick` → 断言 transform）
- 沙箱白名单（`os` / `io` / `require` / `debug` 都不在）、error 和语法错误都不穿透、
  每实体一份 environment 互不污染 —— `lua_vm_smoke`
- 射线查询翻成引擎类型（UUID + glm）—— `physics_raycast_smoke`
- prefab 节点树 → 实体层级 —— `prefab_instantiate_smoke`
- `.lua` 进管线、artifact 和源逐字节相同 —— `asset_pipeline_smoke` 新增那组

**播放器端到端也确认过**（不是人工点的，是读日志）：`arti_player --scene
Assets/Scenes/script_test.artiscene` 起来之后日志里有 `wasd_move attached to Scripted Cube` 和
`standing over 000000000000b005 at y=0.00` —— 说明 `on_create`、`arti.log`、`arti.physics.raycast`
和资产加载这条链全通了，而且没有 error。**键盘输入没法这么验**，那要人在窗口里按。

### 两处反向验证都见了红

1. `lua_vm_smoke`：给 `makeSandbox()` 加上 `sol::lib::os` / `io` / `package` → 「os 库存在」那条红。
2. `script_runtime_smoke`：去掉「抛错后 `disabled = true`」→ 「the failing script was not disabled
   after throwing (x=3.000000, expected 1)」红。**这条是整个脚本层最硬的一条**。

用户拍板的两条（2026-09-04）：

- **脚本是资产**（进 `Assets/`，UUID + `.meta`，走 importer / loader，打包走 artifact）
- **直接 Lua，sol2 + Lua 5.4**。不先做一层 C++ 脚本接口

### 做的时候撞到的五件事

1. **官方 lua 仓库的源码在根目录，不在 `src/`**，而且**没有 `luac.c`**（那个只在发布 tarball 里）。
   GLOB 之后要排除的是 `lua.c` / `onelua.c` / `ltests.c` 三个 —— `onelua.c` 把所有 `.c`
   `#include` 到一起，跟着编就是满屏重复定义。
2. **`enable_language(C)` 必须显式加**：根 `project()` 只声明了 `CXX`。
3. **`lua_vm_smoke` 第一版有个自伤的名字冲突**：第 7 组在真正的全局表里定义了 `on_update`，
   第 8 组再断言「environment 里定义的函数没漏进全局表」就必然红 —— 红的原因是测试自己撞车，
   不是 sol2 漏了。改用 `scoped_callback` 这个从没在全局出现过的名字。
   **教训：验「没漏进全局」必须用一个全新的名字。**
4. **手写 `.meta` 是可行的**，而且这是让示例场景能引用固定 UUID 的唯一办法：
   `Scripts/wasd_move.lua.meta` 里写死 `Handle: 5c81970000000001`，reconcile 只会因为 artifact
   缺失而重导，UUID 原样保留（身份存在 `.meta` 里，而 `ContentHash` / `Size` 只写不读）。
5. **`World::setAssets` 在编辑器里每帧同步一次**，不挂在「项目打开」那个事件上：换项目、关项目、
   读场景失败都会改这个指针，tick 之前无条件设一次是最不容易漏的写法，代价只有一次指针写。
6. **示例场景把「脚本和物理抢 transform」踩出来了 —— 这是本任务最大的一个设计漏洞，已修。**
   用户报的现象是「按住 WASD 物体先卡一下才动」。原因：`World::tick` 的顺序是 FixedUpdate（物理）
   → Update（脚本），物理每步把 body 的位置写回 `TransformComponent`，而那个 body 从来不知道脚本
   写过什么（位置只在 `buildWorld()` 时读过一次）。于是脚本每帧加一点、物理每帧盖回去，直到那个
   body 不再出现在 `moveEvents` 里才停止打架。

   **根因是文档里两条规矩直接冲突**：`Scene.md` 3.1 写「模拟期间 transform 归物理」，3.2 又给了
   脚本 `entity.translation` 的写权限。修法是把所有权按 body 类型定死（新增 Scene.md 3.1.1）：
   `Dynamic` 归物理（写回），`Static` / `Kinematic` 归场景（每步读、不写回）。
   新增 `physics_transform_ownership_smoke` 钉住它。

   **这一轮反向验证逼出了一个我原先没想清的区别**，值得记：两个改动**不等价**，而且各自独立
   就足以让「位移不再被盖掉」成立 —— 所以单独删掉任何一个，第一版测试都是绿的（删两个才红）。
   补了一条**用射线当探针**的断言之后才分得开：
   - `syncSceneOwned()` 是**承重**的那一半 —— 没有它，碰撞体冻在建世界那一刻的位置，
     「视觉动了、挡人的没动、射线打不中」
   - 写回时跳过非 Dynamic 今天只是省一次无用往返，是将来给 Kinematic 接速度驱动时的防线

   **教训**：断言「值没被盖掉」和断言「物理真的在用这个值」是两件事。前者抓不住后者。

7. **`git submodule status` 会把 lua 显示成 `v5.4-beta-336-g6e22fedb`，这不是 pin 错了。**
   `v5.4.8` 是**轻量 tag**，而 submodule status 用的 `git describe` 只认带注释的 tag，
   于是回退到最近的那个（`v5.4-beta`）。实测 `HEAD` 和 `v5.4.8^{commit}` 是同一个
   （`6e22fedb`），`git describe --tags` 也正确显示 `v5.4.8`。**别看到那行就去"修"它。**

开工前已经查清、不用再查的事：

- prefab 实例化只活在 `Tools/scene_editor/src/editor_layer.cpp:631-682`，`Runtime` 层没有 `instantiatePrefab`
- 物理重建信号是 `UpdateContext::frameIndex` 回退（`physics_system.cpp:218-228`），`resetClock()` 会触发。脚本 VM 用**同一个信号**
- Box3D 最近命中射线：`b3World_CastRayClosest`（`box3d.h:100`），命中后 `b3Shape_GetBody` + `b3Body_GetUserData` 就是实体 UUID（物理建 body 时已经把 UUID 塞进 `userData`）
- Edit 模式**不**调 `World::tick()`（`EditorContext::isSimulating()` 才 tick），所以 ScriptSystem 挂 `Update` 自然编辑期不跑，不用再加开关
- importer 注册只在 `AssetPipeline`（`asset_pipeline.cpp:37-39`，三个 importer）；loader 注册只在 `AssetRuntime::finishOpen`（四个 loader）。第五种资产必须**两边都加**，否则「编辑器认得、player 不认得」
- `pack` 只从 `Assets/` 拷 `.artiscene`，别的源文件不进产物（`asset_packer.cpp:298`）。脚本源文件**不会**进打包目录 —— 运行时读的是 `Library/Artifacts` 里那份，和材质一样
- `World` 现在不知道 `AssetManager`。脚本要 `load<ScriptAsset>`，得给它一条路（D6）
- `ninja` / `clang` 不在 PATH 上，改 CMakeLists 之前先补（和前几个任务同一条）

---

## 背景与现状

### 证据 1：游戏逻辑无处可写

`Scene.md`：系统四个 stage，ArtiEngine 只注册了 `PhysicsSystem` 挂 `FixedUpdate`。`Update` / `LateUpdate` 空着在跑。`core::Input` 有了（编辑器相机在用），物理刚体有了，但没有任何地方能写「玩家按空格」。

### 证据 2：prefab 生成是编辑器私货

`spawnAssetEntity()` 里 50 行把 `PrefabAsset::nodes()` 展开成实体（分解 `local_transform`、挂 MeshRenderer、按 `parent` 接层级）。脚本要 `spawn_prefab` 就得先把这段下沉，否则 Lua 绑定会去 `#include` 编辑器。

### 证据 3：射线查询 Box3D 现成，引擎没包

`PhysicsSystem` 的公开头只有 `onUpdate`。`b3World_CastRayClosest` 返回 `b3RayResult { shapeId, point, normal, fraction, hit }`。实体身份已经在 `body_def.userData` 里。差的是引擎侧一个不带 `b3*` 的查询函数。

### 证据 4：`.artimaterial` 是「源文件即资产」的现成样板

`MaterialImporter` 认 `.artimaterial`，`local_id` 为空（一个文件一个资产），artifact 还是 YAML。脚本比它更简单：源是 `.lua` 文本，artifact 也是同一份文本，importer 几乎就是拷贝 + 记一条产出。

---

## 设计决定

### D1 · 脚本是第五种资产 —— 已定（用户拍板）

| | |
| --- | --- |
| 类型串 | `artiengine.asset.script` |
| 源扩展名 | `.lua` |
| artifact | `.artiscript`，UTF-8 文本，内容和源文件逐字节相同 |
| importer | `artiengine.ScriptImporter`，无依赖、无设置、`local_id` 为空 |
| loader | `ScriptLoader`，读成 `ScriptAsset`（就是一段 `std::string`） |

不把 `.lua` 当 `.artiscene` 那种「按路径引用、不是资产」—— 用户明确说了是资产。`pack` 不用改拷源文件那条：artifact 已经在 `Library/Artifacts/**` 里整树拷走。

### D2 · Lua 5.4.8 + sol2 3.5，pin tag，PRIVATE 链 Runtime —— 已定

- `third_party/lua` ← `https://github.com/lua/lua.git` tag `v5.4.8`
- `third_party/sol2` ← `https://github.com/ThePhD/sol2.git` tag `v3.5.0`

**不跟 main**（和 box3d 相反）：这两个是稳定的发布物，跟 main 只会平白吃 breaking change。

官方 lua 没有 CMakeLists。我们在 `third_party/CMakeLists.txt` 里 `add_library(lua STATIC ...)` 编 `src/*.c`，**排除 `lua.c` / `luac.c`**（那是解释器和编译器）。sol2 是 INTERFACE 头。

`artiengine_runtime` PRIVATE 链 `lua` 和 sol2，和 box3d 一样。`ScriptSystem` pimpl，公开头一个 `lua_` / `sol::` 都不出现。

CRT：lua 的 objects 必须是 `/MD`，不要重蹈 box3d 的 `/MT` 坑。我们自己 add_library，默认就跟着工程走，不用再掰。

`SOL_ALL_SAFETIES_ON` 打开。

### D3 · 打开的 Lua 库只有 base / math / string / table —— 已定

不打开 `io`、`os`、`debug`、`package`。v1 没有 `require`。脚本是数据，不该能读硬盘、起进程。

### D4 · 一个实体一个 `ScriptComponent`，v1 没有实例属性 —— 已定

```cpp
struct ScriptComponent {
    arti::asset::AssetHandle<asset::ScriptAsset> script;
};
```

属性表（Godot 那种「脚本声明、Inspector 填」）是另一件事：要反射、要 Inspector 动态画、要决定哪些 key 进快照。WASD 里程碑不需要它——脚本直接读 `Transform` / `Input`。哪天要做，加字段，不改类型串。

### D5 · Lua state 按「模拟会话」重建，不进组件、不进快照 —— 已定

和物理同一个信号：`frameIndex` 不再单调递增 → 拆掉旧 `sol::state`、按当前场景每个 `ScriptComponent` 建一份环境、调 `on_create`。

Stop 时 `copyEntitiesFrom(snapshot)` 把 `ScriptComponent`（handle）拷回去；Lua state 留在 `ScriptSystem::Impl` 里，下次进 Play 会被重建信号清掉。**所以 `registerComponentCopy<ScriptComponent>` 只拷 handle，这是对的。**

每帧还要处理「模拟中新挂上的脚本」：没有实例就 `on_create`；实体没了就 `on_destroy` 并丢掉实例。物理已经处理了「写回时实体没了」，脚本同理。

一个实体一份 `sol::environment`（独立的全局表，共享同一个 `sol::state`），这样两个脚本都定义 `on_update` 不会撞。

### D6 · `World` 持一份可选的 `AssetManager*` —— 已定

脚本要 `load<ScriptAsset>`。`World` 现在不知道资产。

```cpp
void World::setAssets(arti::asset::AssetManager* assets) noexcept;
```

- 编辑器：项目打开 / 关闭时设 / 清（`EditorProject::finishOpen` / `close` 能碰到 `EditorContext`，或者 `EditorContext` 在 `isProjectOpen` 变化时同步）
- 播放器：`AssetRuntime` 打开成功之后、加载场景之前设
- `asset_tools` 不建 `World`，不受影响
- 指针为空时 ScriptSystem 跳过（warn 一次），不要每帧报

**不把 AssetManager 放进 World 构造函数**：编辑器里 World 比项目先存在（先有 context 再 open project）。

### D7 · 出错 = 记 error + 禁用该实例，不许穿过 `World::tick` —— 已定

每个回调走 sol2 的 protected 调用。抛错：`getLogChannel().error(...)`，`instance.disabled = true`，后续帧不再调。编辑器必须能在脚本写错时继续点 Stop。

### D8 · `instantiatePrefab` 下沉到 Engine —— 已定

`engine::instantiatePrefab(scene::Scene&, const asset::PrefabAsset&) -> scene::Entity`（返回根；多根时返回 nodes[0] 那个）。 decomposing `glm::decompose` 的代码从 `editor_layer.cpp` 原样搬过来。编辑器 `spawnAssetEntity` 改成调它。

**不扩展 `PrefabNode`。** 带脚本的子弹是下一件事。

### D9 · 射线查询挂在 `PhysicsSystem` 上，头里不出现 `b3*` —— 已定

```cpp
struct RaycastHit {
    core::UUID entity;
    glm::vec3 point;
    glm::vec3 normal;
    float fraction{ 1.0f };
};
std::optional<RaycastHit> raycast(const glm::vec3& origin, const glm::vec3& translation) const;
```

`translation` 是位移向量（和 Box3D 一致），不是归一化方向 × 长度的另一种写法——绑定到 Lua 时文档里写清楚「从 origin 沿这个向量打出去」。

没有物理世界（还没进过 Simulate，或全部实体被跳过）返回 `nullopt`。

Lua 通过 `Scene::getSystem<PhysicsSystem>()` 调，不另开全局物理单件。

### D10 · v1 绑这些，多了不算 —— 已定

脚本约定三个全局函数，缺了就跳过那个回调（没有 `on_update` 的脚本不是错误）：

```lua
function on_create(entity) end
function on_update(entity, dt) end
function on_destroy(entity) end
```

`entity` 是 userdata：

| 字段 / 方法 | 含义 |
| --- | --- |
| `entity.uuid` | 字符串，只读 |
| `entity.name` | Tag，可读写 |
| `entity.translation` | `{x,y,z}` 表，可读写（写回 `TransformComponent`） |
| `entity.rotation_euler` | 度数，可读写（和 Inspector 一致，内部转四元数） |
| `entity.scale` | `{x,y,z}` 表，可读写 |
| `entity:destroy()` | `scene.destroyEntity` |

全局 `arti`：

| | |
| --- | --- |
| `arti.input.is_key_pressed("W")` | 走 `core::Input`。v1 只认 `A–Z`、`0–9`、`Space`、`Shift`、`Ctrl`、`Escape` |
| `arti.physics.raycast(origin, translation)` | 命中返回 `{uuid, point, normal, fraction}`，否则 `nil` |
| `arti.scene.find_by_tag(name)` | 没有返回 `nil` |
| `arti.scene.spawn_prefab(uuid_string)` | 没有这份资产 / 加载失败返回 `nil`；成功返回根 entity |
| `arti.log.info/warn/error(...)` | 走 `ArtiEngine` 通道 |

**不绑**组件的随便 get/set（没有通用反射）、不绑加组件、不绑场景切换。要动刚体参数就去 Inspector，脚本 v1 只动 transform 和查询。

---

## 任务清单

### 阶段 0 · Lua + sol2 进构建

- [x] **0.1 两个 submodule**
  - `git submodule add` 如上，写进 `.gitmodules`
  - 验收：`git submodule status` 两个都 pin 在 tag 上，不是分支尖

- [x] **0.2 CMake**
  - 文件：`third_party/CMakeLists.txt`、`.gitmodules`
  - 做法：`lua` 静态库编 `src/*.c` 排除 `lua.c`/`luac.c`，公开头是 `src/`；sol2 INTERFACE。`artiengine_runtime` 先**不**链它们（阶段 5 再链），避免还没消费者就把 lua 拖进 player
  - 改 CMake 之前补 PATH
  - 验收：见 0.3

- [x] **0.3 `lua_vm_smoke`**
  - 文件：新增 `ArtiEngine/runtime/tests/lua_vm_smoke.cpp`，只链 `lua` + sol2，**不链 Runtime**
  - 做法：`sol::state` 打开 D3 那四个库，`script("return 1+2")` == 3；再跑一段会 `error()` 的，protected 调用必须抓住、进程不崩
  - 验收：`ctest` 多 1 条，全绿。反向验证：把 `io` 也 open 了的话，`os` 仍然不该存在（断言 `os == nil`）—— 证明 D3 的白名单不是空转

### 阶段 1 · prefab 实例化下沉

- [x] **1.1 `instantiatePrefab`**
  - 文件：新增 `ArtiEngine/scene/prefab_instantiation.{h,cpp}`（Engine 层：它只依赖 Scene + PrefabAsset，不依赖 tick / 物理）
  - 做法：从 `editor_layer.cpp:638-676` 原样搬。空 prefab 返回无效 Entity。`glm::decompose` 失败就留下默认 transform 并 warn
  - 验收：见 1.3

- [x] **1.2 编辑器改调用**
  - 文件：`editor_layer.cpp`
  - 做法：`spawnAssetEntity` 的 prefab 分支改成 `instantiatePrefab` + 选中根 + `requestCommit`
  - 验收：编辑器里拖一个 glTF prefab 进 Viewport，行为与现在逐像素一致（节点数、父子、材质）

- [x] **1.3 `prefab_instantiate_smoke`**
  - 做法：内存里拼一个两节点的 `PrefabAsset`（根 + 子，子带 mesh UUID），实例化后断言实体数、父子、transform、mesh handle
  - 验收：ctest 多 1 条

### 阶段 2 · 射线查询

- [x] **2.1 `PhysicsSystem::raycast`**（D9）
  - 文件：`physics_system.h` / `.cpp`
  - 做法：pimpl 里调 `b3World_CastRayClosest`。没有 world 或 `!hit` 返回 nullopt。`b3Pos` ↔ `glm::vec3` 用已经有的 `toBox3D` / `fromBox3D`
  - 头文件仍然一个 `b3*` 都没有
  - 验收：见 2.2

- [x] **2.2 接到 `physics_smoke` 或新的 `physics_raycast_smoke`**
  - 做法：用 **World**（不是纯 C 的 b3World）摆一个静态地面（y=0 上表面）和一个悬空的盒，tick 几帧让盒落地，然后从 `(0, 10, 0)` 沿 `(0, -20, 0)` 打一枪，命中实体 UUID 是那个盒或地面，`point.y` 在合理范围
  - 没 world 时（刚构造、还没 tick）`raycast` 返回 nullopt
  - 验收：ctest 绿

### 阶段 3 · 脚本资产管线

- [x] **3.1 `ScriptAsset` + importer + loader**
  - 文件：`asset/script_asset.{h,cpp}`、`asset/importers/script_importer.{h,cpp}`、`asset/loaders/script_loader.{h,cpp}`
  - 做法：照 `MaterialImporter` 的骨架但更瘦——`prescan` 返回空（无引用）、`import` 把源文件字节写进 artifact。loader 读成 `std::string`
  - 类型串 / 扩展名见 D1

- [x] **3.2 注册**
  - `AssetRuntime::finishOpen` 加 `ScriptLoader`（**全工程 loader 只这一处**）
  - `AssetPipeline` 打开时加 `ScriptImporter`
  - 验收：见 3.3

- [x] **3.3 `asset_pipeline_smoke` 加一组**
  - 做法：临时目录写 `Scripts/hello.lua`（内容固定一行 `-- hi`），reconcile，断言 catalog 里有一个 `artiengine.asset.script`，load 出来的文本和源逐字节相同
  - 验收：原有断言一条不红，新组过

### 阶段 4 · 组件、序列化、Inspector

- [x] **4.1 `ScriptComponent`**
  - 文件：`components.h`、`component_registration.cpp`、`component_serialization.{h,cpp}`
  - 做法：字段只有 `script` handle。序列化名 `artiengine.script`。拷贝和序列化都登记——漏拷贝 Play 快照会丢掉它（有 warn），漏序列化存盘会丢掉它
  - 验收：见 4.3 和 5.4

- [x] **4.2 Inspector**
  - 文件：`inspector_panel.{h,cpp}`
  - 做法：一节 "Script"，一个 UUID 输入框（照 MeshRenderer 的 mesh 槽，**带 `applied` 缓存**，别再踩 Environment 那个坑）。Add Component 菜单加一项
  - 验收：加组件、填 UUID、存盘再打开还在

### 阶段 5 · ScriptSystem + 绑定

- [x] **5.1 `World::setAssets` + 注册 ScriptSystem**
  - 文件：`world.{h,cpp}`、`editor_context` / `editor_project` / `player_layer`
  - 做法：D6。`World` 构造里 `addSystem<ScriptSystem>(Update)`，和物理一样**全工程只这一处**
  - 编辑器在项目 open/close 时 set/clear；player 在 AssetRuntime 打开后 set
  - Runtime 现在才 PRIVATE 链 lua / sol2

- [x] **5.2 ScriptSystem**
  - 文件：`runtime/script_system.{h,cpp}`
  - 做法：pimpl 持 `sol::state` + `uuid → Instance { environment, disabled }`。重建信号抄物理。加载脚本：`assets->load<ScriptAsset>(handle)` 失败则 warn + disabled。回调 D7
  - 头里不出现 sol/lua

- [x] **5.3 绑定**（D10）
  - 做法：一个 `runtime/script_bindings.cpp`（不进公开头）。`translation` 用表而不是 userdata，少一层；写回时缺字段就保持原值
  - `is_key_pressed` 认不出的键名返回 false 并 warn **一次**（按名字去重），不要每帧刷屏

- [x] **5.4 `script_runtime_smoke`**
  - 文件：`Tools/asset_tools/tests/` 或 `ArtiEngine/runtime/tests/`——需要 AssetPipeline（有 importer）**和** World，所以放 Tools、链 `ArtiTools::Asset` + Runtime
  - 做法：临时项目写 `Scripts/nudge.lua`：
    ```lua
    function on_update(entity, dt)
        local t = entity.translation
        t.x = t.x + 1
        entity.translation = t
    end
    ```
    reconcile → World.setAssets → 建实体挂这个脚本 → `tick(0.016)` 一次 → `translation.x == 1`
    再跑一段 `error("boom")` 的 `on_update`：tick 不抛、第二次 tick `x` 不再涨（被禁用了）
  - 验收：ctest 绿。反向验证：把 D7 的 disabled 去掉，第二次 tick `x` 会再涨 —— 必须看到红

### 阶段 6 · 示例、文档、收尾

- [~] **6.1 示例脚本** ← 进行中：**脚本和场景都写好了，播放器里跑通了；WASD 要人按一下**
  - 文件：`projects/Assets/Scripts/wasd_move.lua`
  - 做法：WASD 平移（Shift 加速）、从实体位置沿 `-Y` 打一枪、命中就 `arti.log.info` 一次（不要每帧）。场景里给某个 Cube 挂上它——**不要默默改 `physics_test.artiscene` 里用户未提交的那份**；改 `1.artiscene` 或新建 `script_test.artiscene`
  - 验收：编辑器 Play：WASD 能动；射线在有地面时打出一条 log
  - **已落地**：`projects/Assets/Scripts/wasd_move.lua`（+ 手写的 `.meta`，固定 UUID
    `5c81970000000001`）和 `projects/Assets/Scenes/script_test.artiscene`（**新建的，没动
    `physics_test.artiscene`**）。场景里地面按文档那条拆成「缩放的视觉 + 不缩放的碰撞体」两个实体，
    被推的方块是 Kinematic（脚本自己写 transform，不想让重力再拽它）。
  - **播放器里验过**（读日志，不是人工点）：`wasd_move attached to Scripted Cube` +
    `standing over 000000000000b005 at y=0.00`，没有 error。
  - **还差**：WASD 的按键要人在窗口里按。射线那条已经证明了绑定是通的。

- [x] **6.2 架构文档**
  - `Assets.md`：五种资产、importer/loader 表
  - `Scene.md`：九个组件、`instantiatePrefab`、`World::setAssets`、`ScriptSystem` 挂 Update
  - `Applications.md`：Play 下脚本真跑；Edit 下不跑的原因（tick 根本没调）
  - `README.md`：缺口表「脚本 / 音频」那行改成「脚本 v1 有了；音频没有。脚本没有实例属性、prefab 带不了脚本、没有 require」
  - 加组件清单旁边补一句：ScriptComponent 的 Lua state **不**进快照，只进 handle

- [ ] **6.3 任务收尾**
  - 头部改已完成，交接区留结论

---

## 端到端验收

1. `cmake --preset debug` + 构建干净，无新增 warning。**已过。**
2. `ctest` 全绿：**15 条**（原 10 + `lua_vm_smoke` + `prefab_instantiate_smoke` +
   `physics_raycast_smoke` + `physics_transform_ownership_smoke` + `script_runtime_smoke`；
   `asset_pipeline_smoke` 多一组）。**已过。**
3. `lua_vm_smoke` 反向验证过：`os` 是 nil。**已过。**
4. `script_runtime_smoke` 反向验证过：去掉 disabled，boom 脚本第二次 tick 还会改 transform。
   **已过**（实测 x=3 而不是 1）。
5. 编辑器：给实体加 Script、填 wasd 脚本 UUID、Play 能 WASD 动、Stop 实体回到原位（Lua 写的
   transform 不落在编辑场景上 —— 这是快照免费给的）。**还没做，要人在 GUI 上点。**
6. 脚本里写 `error("x")`，编辑器不崩，Stop 还能按。**还没做**（逻辑那一半由
   `script_runtime_smoke` 的 boom 用例覆盖了，人工要确认的是编辑器 UI 还能操作）。
7. `arti_player` 跑挂了该脚本的场景。**已过**（除了按键本身）—— 起来了、脚本加载了、`on_create`
   和射线都跑了、没有 error。**「脚本推动实体」这条也在播放器里实测过了**：用一个临时的自动
   前进探针脚本（不需要按键），日志里每 30 帧稳定 +1.0（2.0 单位/秒 × 0.5 秒），线性累加、
   没有卡顿。探针文件和它的 artifact 验完都删了。**只剩「按 W 键这个输入本身通不通」要人试。**
8. Simulate 下脚本也跑（和物理一样，两条轴独立）。**还没做，要人在 GUI 上点。**

**不在验收范围内**：热改 `.lua` 不重导就生效；脚本里生成带 Collider 的子弹（prefab 带不了这些）。

---

## 风险与注意

### 最大的风险是「Lua 异常把 tick 打穿」

sol2 默认有的路径会把 Lua error 变成 C++ 异常。绑定必须走 `protected_function` / `sol::protected_function_result`，并且 **`World::tick` 外层再兜一层** 也不为过——漏一个回调就是编辑器直接没了，Stop 都按不到。5.4 的 boom 用例是这条的防线，不是可选项。

### 第二个风险是「Play 快照把 Lua 指针拷进去」

`ScriptComponent` 里**不许**出现 `sol::environment*` / `lua_State*`。谁把运行时句柄放进组件，`registerComponentCopy` 就会浅拷一份悬空指针，Stop 时炸。头文件里只有 handle，这条从类型上堵住。

### 改 CMakeLists 会弄坏缓存

0.2 / 3.2 / 5.1 都改。PATH 先补。

### `.clang-format` 和实际风格不符

别格式化整文件。

### 工作区里那份未提交的场景

`projects/Assets/Scenes/physics_test.artiscene` **不要 `git add`**。示例场景另建。
