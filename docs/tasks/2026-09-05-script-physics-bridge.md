# 脚本—物理桥接 v2：固定步、连续运动与刚体生命周期

| | |
| --- | --- |
| **状态** | 基本完成（自动验收全过：两条工具链 0 warning、`ctest` 18/18、五处反向验证见红；剩编辑器 / 播放器窗口里的人工操作）|
| **创建 / 最后更新** | 2026-09-05 / 2026-09-05 |
| **涉及仓库** | 只有 ArtiEngine；不修改 ArtiRenderer / ArtiChoco / Box3D submodule |
| **目标** | Lua 在物理固定步之前控制刚体；运动学目标能推动动态物体；运行期间新增 / 删除实体或物理组件不留下漏建 / 幽灵刚体。编辑器和播放器继续共用 World |
| **明确不做** | 触发器 / 碰撞回调、角色控制器、游戏 Prefab、脚本参数 / 热重载、渲染插值、父级 / 缩放支持、运行中修改碰撞体尺寸与材质、任务系统并行化。完整终点关卡是后续任务，不以本次桥接冒充完成 |

## 交接区

**当前进度：全部五步落地，自动验收全过。剩下的只有编辑器 / 播放器窗口里的人工操作那几条。**

两条工具链各自干净：`cmake --build --preset debug`（clang 22.1.3）和 `--preset msvc-debug`
（cl，dev shell 里跑）都是 **0 warning**，`ctest` 两边都 **18/18**（原 15 + 三个新增）。

**下一步（都要人在窗口里做，代码这边没有待办）：**

1. 编辑器 Play / Simulate 里跑 `Scenes/physics_move_test.artiscene`：WASD 走、Shift 加速、
   Space 跳、R 复位；走上升降平台看是不是被托着一起动；撞两个箱子看推不推得动。
2. Stop 之后场景回到编辑时的样子（玩家和箱子都回原位）。
3. 重复 Play → Stop → Play，确认不继承上一次的 VM 和物理状态（**只跑一帧就 Stop** 那种也试一次，
   那是 D5 专门修的）。

**工作区保护：** `projects/Assets/Scenes/physics_test.artiscene`、
`projects/Assets/Skybox/citrus_orchard_road_puresky_1k.hdr` 及其 `.meta` 是用户已有改动，
本次一个字没动、没暂存。`script_test.artiscene` 和 `wasd_move.lua` 也原样保留 —— 新示例是**另建**的。
submodule 指针没动、没提交任何东西。

### 验证记录（这一轮真跑过的命令）

| 做了什么 | 结果 |
| --- | --- |
| `cmake --build --preset debug`（改动文件全部强制重编） | 0 warning |
| `ctest --preset debug` | 18/18 |
| `cmake --build --preset msvc-debug`（dev shell，同样强制重编） | 0 warning |
| `ctest --preset msvc-debug` | 18/18 |
| `asset_tools scan projects/projects.artiproj` | 两份新脚本 reimported（缺 artifact），手写 `.meta` 里的 UUID 原样保留 |
| `arti_player --scene Assets/Scenes/physics_move_test.artiscene` | 起来了，日志有 `physics_move attached to Player` + `standing on 000000000000c005 at y=0.00`，**没有 error、没有物理 skip warning** |

**键盘输入没法这么验**（要真的窗口焦点 + 真的按键事件），所以上面「下一步」那三条留空着。

### 五处反向验证都见了红

每一条都是「把实现改回旧写法 → 看指定的断言变红 → 改回来」。

| 改回什么 | 哪条红了 |
| --- | --- |
| Kinematic 退回每步 `b3Body_SetTransform` | `盒子没被平台托起来（y 从 0.749719 到 0.568864）` —— 传送不但托不起来，还把盒子按下去了 |
| 不拆不再合格的 body | `实体删了，射线还打得中它 —— 幽灵刚体` |
| `World::resetClock` 不通知两个系统（只靠帧号回退） | `重开会话之后 body 还在老地方` |
| 脚本固定回调**之前**不同步 body | `第一个固定步里设的速度没生效（x=-0.000015）` |
| 固定回调抛错只禁用渲染回调 | `固定回调抛错之后 on_update 还在跑` |

### 实现和设计决定的六处出入（都是做的时候才看清的）

1. **`syncBodies` 一个固定步调两次，两次都有各自的必要性。** D4 只说了「同步发生在每个物理步
   边界」，实际是：脚本的固定回调**之前**要有一次（不然会话第一个固定步里 `apply_force` 因为
   「还没有 body」返回 false），`b3World_Step` **之前**还要有一次（不然脚本这一步写的运动学目标
   晚一步才生效）。两次之间没有 step，所以第二次是幂等的。顺序写在 `World::tick` 里 —— 那是唯一
   同时认识这两个系统的地方。
2. **「目标停止后消除残余速度」是免费的**，不需要 D2 说的那步显式清零。读了 box3d 的
   `src/body.c` 才确认：对**醒着的** body，`b3Body_SetTargetTransform` 每次都把速度重写一遍，
   目标没变就是写 0。风险清单里那条「旧速度不能留着继续积分」只在 body **睡着**时成立。
3. **多出一条 D2 没料到的：目标远到一步搬不过去时必须退回传送。** 线速度会被世界的
   `maximumLinearSpeed`（400 m/s）截、角速度会被 `B3_MAX_ROTATION`（每步 45°）截，body 会落在
   半路上。发现方式是**已有的** `physics_transform_ownership_smoke` 变红了：它把一个 Kinematic 体
   一步挪 12 米（722 m/s）然后立刻用射线探。加上这条退路之后那个测试原封不动就绿了 —— 这也说明
   「碰撞体就在场景说的地方」这条不变量没被这次改动破坏。
4. **睡着 body 的那一下传送补偿不是承重的**（老实说）。删掉 `physics_kinematic_smoke` 照样全绿，
   因为「基准取 body 当前位置」本身就自我纠偏（欠的距离攒到超过阈值就唤醒追上，变成毫米级顿走）。
   留着是为了「目标动一点点就停下」那种情况 —— 那时候欠的距离永远攒不到阈值。理由写在代码注释里。
5. **控制接口在 C++ 侧不记日志，警告放在 Lua 绑定那一层**，按 (操作, 实体) 去重报一次。理由：
   调用方是每帧跑的脚本，在库那一层报就是每帧一条；而绑定那边才分得清「缺组件 / 类型不对 /
   被物理跳过」三种原因 —— 它们的修法完全不同。
6. **`teleport` 的签名多一个 `Scene&`。** D3 那张表里它只收 entity，但它必须同时改
   `TransformComponent`：Static / Kinematic 的权威值在场景那边，只挪 body 会在下一次同步时被拉回去。

### 两处超出文档字面的东西

- **多了一个测试 `example_assets_smoke`。** 任务清单第 4 步要求「示例通过资产导入 / 场景加载
  自动验证」，但没说怎么验。做法是：编译那三份 `.lua`（沙箱四个库 + 一个 `arti` 替身）、读示例
  场景、检查手写 `.meta` 里的 UUID 和场景里的引用对得上、并断言凡是带 RigidBody + Collider 的
  实体都没有父级、缩放都是 1。示例只有人在 GUI 里才会看，所以是最容易悄悄烂掉的一类资产。
- **多了一份示例脚本 `platform_lift.lua`。** 第 4 步字面只要求「WASD 驱动动态物体 + R 复位」，
  但那样场景里就没有任何东西演示这次最关键的修复（运动学体推得动 Dynamic 了）。升降平台是 22 行，
  换来一个能实际站上去的验收对象。

## 背景与现状

2026-09-05 执行源码检索确认：

```powershell
rg -n 'runSystems|addSystem' ArtiEngine/runtime/world.cpp
rg -n 'on_update|on_fixed_update' ArtiEngine/script/script_system.cpp
rg -n 'SetTransform|buildWorld|DestroyBody' ArtiEngine/physics/physics_system.cpp
```

- `World` 只在 `FixedUpdate` 注册 PhysicsSystem，ScriptSystem 在 `Update`；每帧先补全部固定步，再跑脚本。
- Lua 只有 `on_create` / `on_update` / `on_destroy`，`arti.physics` 只有 raycast。
- Static / Kinematic 每步都用 `b3Body_SetTransform`，没有连续目标速度；Dynamic 独占物理解算后的 transform 写回。
- 物理仅在首次 tick / 帧号回退时建世界。实体删除后只跳过 transform 写回，没有 `b3DestroyBody`；新实体不会自动建 body。
- 当前 ScriptSystem 一边遍历 EnTT view 一边执行可销毁实体的 Lua；需要改成快照身份后派发。
- 本地 Box3D 头文件提供 `b3Body_SetTargetTransform`、中心施力 / 冲量、线速度读写，且类型只留在物理实现文件中。

## 设计决定

### D1 · 已定：一个 Lua VM / 实例，同时服务两种回调

新增可选 `on_fixed_update(entity, fixed_dt)`；保留 `on_update(entity, dt)` 的每渲染帧一次语义。不注册第二个 ScriptSystem，不创建第二份 Lua 状态。固定步脚本在物理同步和解算前派发；固定步和普通回调共用禁用状态，任一回调抛错后都停用该实例。

### D2 · 已定：运动 / 传送分开，保留 transform 所有权

- Dynamic：只用物理控制接口推动；物理解算后写回 transform。
- Kinematic：脚本写 transform 作为下一固定步目标；通过 `b3Body_SetTargetTransform` 求线 / 角速度，不再每步传送。目标停止后消除残余速度。
- Static：只有 transform 改变时传送更新。
- 显式 teleport：更新物理位置和场景位置，清零线 / 角速度；不把复位误当高速运动。

### D3 · 已定：最小物理控制接口

Lua 接口放在 `arti.physics`，接收 Entity，而非 Box3D id：`get_linear_velocity(entity)`、`set_linear_velocity(entity, vec3)`、`apply_force(entity, vec3)`、`apply_impulse(entity, vec3)`、`teleport(entity, position)`。

速度写入、力、冲量仅用于 Dynamic，Kinematic 仍以 transform 为目标。操作失败返回 false，速度查询失败返回 nil；无效实体、无刚体、错误刚体类型、非有限输入不能触发物理库断言。

### D4 · 已定：每次物理同步维护 UUID → body 映射

只为同时具有 RigidBody / Collider、无父级且单位缩放的实体建 body。同步时创建新 body，移除失去实体 / 必需组件 / 合法变换条件的 body；同 UUID 重新创建的实体不能复用旧物理身份。类型变化重建，其他参数的运行时热修改仍不在范围内。

同步发生在每个物理步边界，不在 Box3D step 中修改世界。Lua 回调先快照实体身份，再逐个重新验证，不跨回调保存 EnTT 组件引用。

### D5 · 已定：显式会话重置

World::resetClock 同时重置物理和脚本会话，避免只运行一帧便 Stop / Play 时 `0 → 0` 无法识别新会话。组件 / 快照里仍不存运行时句柄。

## 任务清单

- [x] **1. 固定步脚本与会话边界**
  - 文件：`ArtiEngine/runtime/world.*`、`ArtiEngine/script/script_system.*`
  - 做法：共享实例派发固定回调，安全遍历，显式重置，保持 enabled 语义。
  - 验收：0 / 1 / 多固定步、`on_create` 一次、两类回调共享状态、固定回调错误隔离、reset 后新会话、回调自删 / 删除其他实体。
  - **已过**：`script_physics_smoke` 的 7 节全绿（cadence / 同步生效 / 绑定失败路径 / 共享禁用 /
    回调里删实体 / 帧率无关 / 会话重置），`physics_kinematic_smoke` 的第 6 节盖住「只跑一帧就重开」。
- [x] **2. 连续运动与生命周期同步**
  - 文件：`ArtiEngine/physics/physics_system.*`
  - 做法：维护 body 身份映射；新增 / 删除 / 移除组件同步；Kinematic 目标速度、Static 按需同步；提供控制 API。
  - 验收：移动平台托起动态盒、目标停止无漂移、低速 / 旋转目标、刚体控制和 teleport、新增能参与模拟、删除后 raycast 不再命中、非法输入安全失败。
  - **已过**：`physics_kinematic_smoke` 六节全绿。主断言不是位置而是**被托着那个盒子的速度**
    （vy > 4，而位置重叠推出来的那种被 `contactSpeed` 3 m/s 截着）—— 这一条才分得开两种实现。
- [x] **3. Lua 物理绑定与端到端测试**
  - 文件：`ArtiEngine/script/detail/script_bindings.cpp`、`Tools/asset_tools/tests/`、相关 CMakeLists。
  - 做法：绑定最小接口；用真实 `.lua` 导入、挂实体、`World::tick` 验证，而非只调用裸 Lua。
  - 验收：固定脚本施力在同一步生效；不同渲染步长产生相同固定步结果；原 `script_runtime_smoke` 仍通过。
  - **已过**：`script_physics_smoke` 走完整条路（写 `.lua` → reconcile → 挂实体 → `World::tick`）。
    30 / 60 / 120 Hz 各跑 1.005 秒都是 60 个固定步、结果一致（刻意不取整秒，那正好在 59 / 60 的
    边界上）。`script_runtime_smoke` 一个字没改，仍绿。
- [x] **4. 示例与架构文档**
  - 文件：新建 `projects/Assets/Scripts/physics_move.lua`（+ `platform_lift.lua`）与
    `Scenes/physics_move_test.artiscene`，`docs/Architecture/Scene.md` / `Applications.md` / `README.md`。
  - 做法：WASD 通过固定步物理接口驱动动态物体，R 显式复位；示例保留旧 wasd 测试，不改用户场景。
  - 验收：示例通过资产导入 / 场景加载自动验证；人工操作和可视化验收另列，不伪装成已通过。
  - **已过（自动那半）**：新增 `example_assets_smoke` —— 编译三份 `.lua`、读示例场景、核对手写
    `.meta` 的 UUID、并断言带刚体的实体都无父级且缩放为 1。`asset_tools scan` 和 `arti_player`
    也真跑过（见验证记录）。**人工那半在「下一步」里，没有勾。**
- [x] **5. 构建、回归与交接**
  - 文件：本任务交接区、`docs/tasks/README.md`。
  - 做法：先针对新增测试构建运行，再运行完整 ctest，检查 diff 和 submodule / 用户资产状态。
  - 验收：记录真实命令与结果，未完成的人工验收明确留空，不提交代码。
  - **已过**：两条工具链各强制重编一遍、各 0 warning、各 18/18。没有提交、没动 submodule 指针、
    用户那两个未提交的文件一个字没碰。

## 端到端验收

1. Debug 构建成功；新增物理 / 脚本桥接测试及既有回归通过。**已过**（clang 和 MSVC 两条工具链
   各 0 warning、各 18/18；新增三个测试，原 15 个一个没红）。
2. 固定脚本在同一个物理步产生运动，30 / 60 / 120 Hz 渲染驱动的一秒模拟结果近似一致。**已过**
   （`script_physics_smoke`：只 tick 一帧就能看到速度生效；三种帧率都是 60 个固定步、结果一致）。
3. 运动学平台能托起动态盒，停止目标后平台不漂移；显式 teleport 不留下速度。**已过**
   （`physics_kinematic_smoke`：盒子自己的 vy > 4；停下后射线量平台上表面误差 < 0.02；
   teleport 之后速度长度 < 1e-4）。
4. 运行时新增实体产生 body；删实体 / 删必需组件后碰撞和射线不会继续引用旧 body。**已过**
   （`physics_kinematic_smoke` 第 5 节，另外还盖了「挂上父级」「换刚体类型」「同 UUID 重建」）。
5. 固定回调错误不会逃出 `World::tick`；自删 / 删除其他脚本实体不损坏遍历；重复 Play 不继承旧 VM
   或物理状态。**已过**（`script_physics_smoke` 第 4、5、7 节）。
6. 人工：编辑器 Play / Simulate 中 WASD 和 R、Stop 恢复编辑场景、脚本错误后能 Stop；播放器同场景
   行为一致。没有实际操作时不得勾选。**没做** —— 要真的窗口焦点和真的按键事件。播放器只验到
   「起来了、脚本挂上了、射线打中地面了、没有 error」这一步（见交接区的验证记录）。

## 风险与注意

- Box3D `SetTargetTransform` 在速度低于 sleep threshold 时不应用目标。**这条只在 body 睡着时成立**
  （实现时读 `src/body.c` 确认的）：醒着的 body 每次调用都会被重写速度，目标没变就是写 0，所以
  「旧速度留着继续积分」不会发生，也不需要显式清零。见交接区那六处出入的第 2 条。
- 四元数按 glm `(w, x, y, z)` 与 Box3D `.s / .v` 显式转换；不要替换成分量猜测。
- 每帧可能有零个或多个固定步，不能把“每帧输入 / `on_update`”改成“每固定步执行”。
- 子模块三层；本任务不改其 API、不更新其指针。
- `.clang-format` 与当前风格不完全一致，不格式化整文件。测试需要正确的 CMake / LLVM 工具链环境。
- 原 Lua 任务仍有三项编辑器人工验收，本任务的自动测试不能代替那三项。
