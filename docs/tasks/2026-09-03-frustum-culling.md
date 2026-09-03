# 视锥剔除：让 `culled` 不再恒为 0

| | |
| --- | --- |
| **状态** | 已完成 |
| **创建** | 2026-09-03 |
| **最后更新** | 2026-09-03 |
| **涉及仓库** | **ArtiRenderer**（几乎全部改动）→ ArtiEngine（推指针 + 打开测试开关 + 文档）。**两层 submodule，不是三层** —— 剔除代码在 `ArtiRenderer/ArtiRenderer`，那是 ArtiRenderer 这个 submodule 自己的库，不是 ArtiChoco |
| **目标** | 加一个 `Frustum` 类型和 AABB 相交测试，接到三个绘制循环上（G-Buffer / 拾取 / 阴影四级），把 `FrameStatistics::culled` 填上。**阴影那边按每级的「拉长视锥」剔，不是按相机视锥** |
| **明确不做** | 不做并行化（先单线程做对，见 D6）。不做遮挡剔除、不做 BVH / 空间划分、不做每级 cascade 的 LOD |

---

## 交接区

> **全文唯一允许改动的段落。每次收工前更新这里。**

**当前进度：已完成。** 四个阶段都验收过，指针已推到本仓库。

阶段 3 画面验收（执行者 2026-09-03 在编辑器里看过）：`shadow culled` 是正数、影子不闪、
本体出画影子还在。那三条是这条剔除唯一真正重要的防线，过了才能动文档。

阶段 1 落地的东西：
- `CMakeLists.txt` —— `ARTIRENDERER_BUILD_TESTS=ON`（1.1）
- `ArtiRenderer/ArtiRenderer/include/frustum.h` —— 纯头文件，`fromViewProjection` + `intersects`（1.2）
- `ArtiRenderer/tests/frustum_test.cpp` —— 35 条断言，透视 20 + 正交 15（1.3）

阶段 2 落地的东西：
- `FrameContext` 构造时算相机可见位（`vector<uint8_t>`，越界 / 空盒一律当可见）
- `GBufferPass` 两趟都跳过被剔的 draw，只在绘制趟数 `culled`
- `PickingPass` 读同一份可见位，不数 `culled`
- 编辑器状态栏 `| FPS | draws | culled`

**阶段 2 验收（执行者在编辑器里看过，2026-09-03）：** 转相机 `culled` 从 0 变正、转回来变回 0，
`draws + culled` 守恒，视锥边缘看得见的都点得到。

阶段 3 代码已落地，**画面还没对过**：
- `ShadowCascadeResult::caster_visible` 打平存 4 × draws，XY 重叠循环里顺手置位
- `ShadowPass` 按它跳过，数 `shadow_culled`
- 空盒不参与 near/far 收紧（修了一个预存在的坑：空盒八个角变换会污染 min_z / max_z）
- 编辑器 / 播放器都显示 `shadow culled`

**阶段 3 要盯的：** `shadow_culled > 0`；转相机影子不许闪；**本体在画面外、影子在画面内**
那个情形影子必须还在。拿相机视锥剔阴影的话这一条会挂。

**阶段 1 唯一的意外，会影响后面：D3 原来的说法有一半是错的。**「near 面写成 NO 公式会导致
几乎什么都不剔」只对**正交**矩阵成立，对透视矩阵只是把近平面挪 0.05，肉眼和纯透视的单元测试
都抓不住。D3 已经改成两行一张表，1.3 里记了完整的复现过程。**阶段 3 的阴影 cascade 走
`orthoRH_ZO`，平面提取的符号问题会真的咬人，别指望阶段 1 的透视用例挡住它。** 这次阴影剔除
没再走一遍 Frustum 提取（光空间 XY 重叠），所以 D3 那个坑在这条路径上没有被踩到，但谁以后
给 cascade 改成 `Frustum::fromViewProjection(ortho)` 就必须带着正交用例。

顺带发现（不在本任务范围）：`task_system_test` 里「parallelFor 被至少两个线程跑过」那条断言
偶发红，连跑 20 次复现 1 次。跟这次的改动无关，见 1.1。

### 一条构建环境的坑（上一个任务撞过，代价是 cmake 缓存被弄坏）

**`ninja` 和 `clang` 都不在普通 shell 的 PATH 上。** 它们在 VS 18 Community 里：

```
/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja
/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin
```

平时 `cmake --build --preset debug` 能跑是因为它不需要重跑 configure。**一旦改了任何
`CMakeLists.txt`**（本任务的 D2 和阶段 1 都会改），ninja 会触发 re-configure，那一步找不到
`ninja` 就把 `CMakeCache.txt` 里的编译器打回 `UNINITIALIZED`。**修法**：把上面两个目录
prepend 到 PATH，重跑一次 `cmake --preset debug` 修好缓存（对象文件还在，不用全量重编）。

---

## 背景与现状

### 证据 1：剔除完全不存在

`FrameStatistics::culled` 声明在 `ArtiRenderer/ArtiRenderer/include/renderer.h:48`。
全工程（去掉 imgui 的 third_party）**没有一处写它**：

```
$ grep -rn "culled" --include=*.h --include=*.cpp ArtiRenderer/ArtiRenderer ArtiEngine Runtime Tools
ArtiRenderer/ArtiRenderer/include/renderer.h:48:    uint32_t culled{ 0 };
Runtime/player/src/player_layer.cpp:200:    ImGui::Text("%u draws / %u submeshes / %u culled", ...
```

一个声明、一个显示，中间没有生产者。播放器 UI 上那个数字恒为 0。

`Frustum` 这个类型也不存在 —— `include/` 下只有 `aabb.h`。

### 证据 2：`world_bounds` 已经算好了，但只用来拟合、没用来剔除

`DrawItem::world_bounds`（`include/render_scene.h:35`）每帧由抽取填：
`ArtiEngine/scene/render_scene_extractor.cpp:138`。

现在它唯一的消费者是 `src/pipeline/shadow_cascades.cpp:85-87` —— 拿去算 cascade 的
near / far。**没有任何 pass 用它做可见性判断。**

### 证据 3：三个绘制循环都无条件全遍历

```
src/pipeline/passes/gbuffer_pass.cpp:266   （材质常量预写那趟）
src/pipeline/passes/gbuffer_pass.cpp:279   （真正的绘制趟）
src/pipeline/passes/picking_pass.cpp:278
src/pipeline/passes/shadow_pass.cpp:241
```

三者的跳过条件目前完全一致：`!resolved || material.type != PBR`。这个「逐条对齐」是刻意的，
两处注释都写了理由（`shadow_pass.cpp:243`、`picking_pass.cpp:284`）：G-Buffer 不画的东西
屏幕上不存在，阴影那边要是画了就会投出没有本体的影子，拾取那边要是画了就会选中看不见的实体。
**加剔除时这个不变式要重新想 —— 见 D4。**

### 证据 4：代价的量级在阴影这边

`shadow_pass.cpp:229` 是 `for (index < kShadowCascadeCount)`，里层 `:241` 又是完整的
`scene.draws`。四级 cascade 各自把**整个场景**画一遍，加上 G-Buffer 那遍，几何一帧走五遍，
其中四遍没有任何剔除。一个只属于最远那级的物体，现在四级各画一次。

### 证据 5：阴影那边已经有一个「XY 是否重叠」的测试，算完扔了

`shadow_cascades.cpp:158-165`：

```cpp
for (const auto& bounds: caster_bounds) {
    if (bounds.max.x < min_x || bounds.min.x > max_x || bounds.max.y < min_y ||
            bounds.min.y > max_y) {
        continue;                       // ← 这个 continue 就是「这个 draw 不影响这一级」
    }
    min_z = std::min(min_z, bounds.min.z);
    max_z = std::max(max_z, bounds.max.z);
}
```

它是为了收紧 near / far 而做的，但**判据正好就是阴影剔除要的那个**（理由见 D3）。
`caster_bounds` 也已经是「每个 draw 在光空间的 AABB」了。所以阶段 3 的主要工作是
**把这个已经在算的结果留下来**，而不是新写一套测试。

### 证据 6：测试设施在，但一条都没进 ctest

`ArtiRenderer/tests/` 下有 `test_check.h`（极简断言收集器，退出码即结果）和三个测试
（`texture_desc_test` / `handle_test` / `aabb_test`），用 `artirenderer_add_test()` 注册。

而根 `CMakeLists.txt:47` 把开关强制关了：

```cmake
# ArtiRenderer 这里是被当依赖库消费的，它自己的 samples 和 tests 不需要跟着建。
set(ARTIRENDERER_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ARTIRENDERER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
```

所以那三个测试**在本仓库里从来没跑过**。这和刚删掉的 `examples/test_app` 是同一个坑：
不在默认构建里的东西会静默腐烂。见 D2。

### 证据 7：深度约定是 ZO，而且是显式写的

```
ArtiEngine/scene/render_scene_extractor.cpp:43:  glm::perspectiveRH_ZO(...)
Tools/scene_editor/src/editor_camera.cpp:145:    glm::perspectiveRH_ZO(...)
ArtiRenderer/ArtiRenderer/src/pipeline/shadow_cascades.cpp:174: glm::orthoRH_ZO(...)
```

**全工程没有定义 `GLM_FORCE_DEPTH_ZERO_TO_ONE`**（grep 过，只有 third_party 里有）——
靠的是显式用 `_ZO` 后缀的函数。这一条直接决定平面提取的公式，见 D3。

---

## 设计决定

### D1 · `Frustum` 放公开头 `include/frustum.h` —— 已定

和 `aabb.h` 并排。理由：

- `AABB` 已经是公开的，因为 `DrawItem::world_bounds` 是公开契约的一部分。`Frustum` 是它的
  天然配对物，两个都是纯数学类型、只依赖 glm、没有 NVRHI 也没有 Vulkan。
- 测试要能 include 它。`ArtiRenderer/tests/` 里的测试链 `ArtiRenderer::Renderer` 并直接
  `#include "aabb.h"`，公开头才有这个待遇。
- 将来抽取那边想做预剔除（把不可见的 draw 干脆不塞进 `RenderScene`）也需要它是公开的。

**不放** `src/pipeline/culling.h`：那样测试就只能测到 pass 层面，而平面提取的符号约定是这次
最容易写错的地方，它必须能被单独断言。

### D2 · 根 `CMakeLists.txt` 打开 `ARTIRENDERER_BUILD_TESTS` —— 已定

改 `CMakeLists.txt:47`，把 `ARTIRENDERER_BUILD_TESTS` 从强制 OFF 改成 ON（`SAMPLES` 保持 OFF）。

理由：这次要加的 `frustum_test` 如果进不了 `ctest`，它和刚删掉的 `test_app` 就是同一种东西 ——
写完就开始腐烂。而 `ARTIRENDERER_BUILD_SAMPLES` 必须保持 OFF：`tests/CMakeLists.txt:16` 之后
那几个 smoke 用例需要真窗口和 Vulkan surface（`basic_window_imgui_smoke` 之类），不适合
进默认 ctest。三个纯单元测试不需要显示设备。

**已实测**（这是开工前做的，所以 D2 不是赌）：

```
$ cmake -S ArtiRenderer -B <tmp> -G Ninja -DARTIRENDERER_BUILD_TESTS=ON -DARTIRENDERER_BUILD_SAMPLES=OFF
$ ctest --test-dir <tmp>
1/3 Test #1: texture_desc_test ......   Passed
2/3 Test #2: handle_test ............   Passed
3/3 Test #3: aabb_test ..............   Passed
100% tests passed out of 3
```

所以打开它是白捡三条 ctest，不是引入三个待修的失败。**代价**：`artirenderer_add_test()` 会调
`artichoco_stage_vulkan_sdk_runtime()`，于是每个测试目标旁边会多拷一份运行时 DLL。可以接受。

### D3 · 平面提取用 Gribb-Hartmann，ZO 约定下 near 面取「第 2 行」而不是「第 3 行 + 第 2 行」—— 已定

从 `view_projection` 直接取六个面（行向量组合），不走「反解八个角再叉积」那条路 ——
后者要多算一个逆矩阵，而且角点顺序写错的时候症状是「某个方向剔多了」，很难查。

**ZO 和 NO 的区别只在 near 面**，这是这次最容易埋进去的 bug：

| 约定 | near 面 | far 面 |
| --- | --- | --- |
| NO（OpenGL 默认，`z ∈ [-1,1]`） | `row3 + row2` | `row3 - row2` |
| **ZO（本工程，`z ∈ [0,1]`）** | **`row2`** | `row3 - row2` |

证据 7 已经确认全工程用 `perspectiveRH_ZO` / `orthoRH_ZO`。写成 NO 的公式不会崩、不会报错，
但**症状在透视和正交下完全不同** —— 这一条是阶段 1 实测出来的，最初的写法（下面划掉那句）
是错的：

| 矩阵 | ZO（`row2`） | NO（`row3 + row2`） | 差别 |
| --- | --- | --- | --- |
| `perspectiveRH_ZO(near=0.1, far=100)` | near 面在 `z = -0.1` | near 面在 `z ≈ -0.05` | 只是近处多一条 0.05 的缝，**看不出来** |
| `orthoRH_ZO(near=1, far=100)` | `z <= -1` | `z <= +98` | **几乎什么都不剔** |

原因是正交矩阵的 `row3` 是常量 `(0,0,0,1)`，加上去等于把整条平面平移出去；透视的 `row3`
含 `-z`，加上去只是轻微挪动，方向不变。

所以：~~写成 NO 会「几乎什么都不剔」，阶段 1 必须有一条「相机背后的盒子被剔掉」的断言~~ ——
**透视用例抓不住这个 bug**。实测过：把 near 改成 `row3 + row2`，纯透视的 20 条断言全过。
有效的防线只有**正交**用例（`orthoRH_ZO` + 相机后方的盒子），而正交正是阴影 cascade 走的路
（`shadow_cascades.cpp:174`）。只有透视用例的话，这个 bug 会一路溜到阶段 3 的阴影才发作。

左右上下四个面：`row3 ± row0`、`row3 ± row1`。六个面都要归一化（除以法线长度），否则
「AABB 到平面的距离」这个量纲不对，保守测试会偏。

### D4 · 三个 pass 用**不同**的视锥，但共用一个不变式 —— 已定

证据 3 说的「跳过条件逐条对齐」在加了剔除之后必须重新表述。新的不变式是：

| pass | 用哪个视锥 | 剔掉的后果 |
| --- | --- | --- |
| `GBufferPass` | 相机视锥 | 屏幕上不存在 —— 这就是目的 |
| `PickingPass` | **同一个**相机视锥 | 安全：剔掉的东西屏幕上也没有，点不到才是对的 |
| `ShadowPass` | 每级 cascade 的**光空间 XY 范围**（不是相机视锥） | 见 D5 |

关键点：**拾取必须和 G-Buffer 用同一个视锥、同一套判据**。两边用不同的剔除条件，症状是
「看得见但点不到」或「点到了看不见的东西」，而且只在视锥边缘出现。所以剔除结果要算一次、
两个 pass 读同一份 —— 见 D7。

G-Buffer 那两趟循环（材质常量预写 `:266` + 绘制 `:279`）也必须用同一份可见集合，否则会给
已经剔掉的材质写常量（浪费，但无害），或者更糟：漏写一个还要画的材质的常量。

### D5 · 阴影剔除按「光空间 XY 重叠」判，这在数学上就是拉长后的视锥 —— 已定

**不能拿相机视锥剔阴影投射体。** 一个物体本体在相机视锥外、影子却落进画面，是完全正常的
情形；按相机视锥剔会让影子凭空消失。架构文档 `README.md:259` 那条「剔除要按每级的光锥做」
说的是这件事，但没说透判据。

正确的判据是「把这一级的正交盒沿光照方向朝光源无限拉长，物体是否和它相交」。而对**方向光的
正交投影**，这个测试可以精确化简成：**物体在光空间的 AABB 与这一级的 `[min_x,max_x] ×
[min_y,max_y]` 是否重叠**。理由是正交投影下 XY 不随深度变化（没有透视缩小），所以「沿 -Z
拉长」在 XY 上不改变覆盖范围。

**Z 方向不用判**，这一点要靠 `shadow_cascades.cpp:156-165` 的逻辑成立：那段代码把 `min_z` /
`max_z` 撑到「所有 XY 重叠的投射体」的 Z 范围之外。换句话说 **near/far 是按投射体撑开的，
所以任何 XY 重叠的物体都必然落在这一级的 Z 范围内**。这是个真依赖：将来谁把 near/far 改成
固定值或按场景 AABB 算，这条剔除就会开始剔掉合法的投射体。**代码里必须写下这个依赖。**

只对方向光成立。点光 cubemap 阴影和聚光阴影（架构 `README.md:258` 里还没做的那些）要另写
判据，不要照抄。

### D6 · 先单线程做对，并行留到确认有收益之后 —— 已定

`ArtiRenderer` PUBLIC 链 `ArtiChoco::Core`（`ArtiRenderer/CMakeLists.txt:58-61`），而
`TaskSystem` 就在 `artichoco/core/task/`（`artichoco/core/CMakeLists.txt:8-10`）里 ——
**所以并行化在分层上是通的**，`src/detail/log.h` 已经在 include `artichoco/core/log.h` 了。

但这次不做。理由：先并行会让「剔错了」和「并行错了」两类 bug 缠在一起，而剔除的正确性
（尤其 D3 的符号和 D5 的判据）是这个任务的全部难点。而且现在没有数字证明剔除本身是瓶颈 ——
它是 O(draws × 平面数) 的纯算术，几百个 draw 的场景里可能根本不值得开线程。

**接缝**：`Frustum::intersects(AABB)` 是无状态纯函数，可见集合是「每个 draw 一个 bool」的
`vector`。要并行就是把填这个 vector 的循环换成 `parallelForRanges` —— 无共享写、语义不变。
拿到 `culled` 的真实数字之后再决定，那是独立一个任务。

### D7 · 可见集合算一次，存 `FrameContext` —— 已定

`FrameContext` 每帧由 `Renderer::renderFrame` 构造一次（`src/renderer.cpp:135`），所有 pass
共享（`src/pipeline/frame_context.h:32` 的注释：「一帧里所有 pass 共享的东西」）。相机可见集合
存这里，`GBufferPass` 的两趟和 `PickingPass` 都读它 —— 这是 D4 那个「必须同一份」的实现手段。

**阴影的可见集合不能预存在 FrameContext 里**：cascade 是在 `ShadowPass::record()` 里才算出来的
（`shadow_pass.cpp:216`），而剔除依赖 cascade 的 XY 范围。所以阴影那份由
`computeShadowCascades()` 一并返回（它本来就在算，见证据 5），存在 `ShadowCascadeResult` 里。

### D8 · `culled` 的语义 = 被相机视锥剔掉的 submesh 数 —— 已定

不含阴影剔掉的。理由：`draw_calls` 和 `submeshes` 现在都是「场景里画了多少」的意思
（`gbuffer_pass.cpp:318-319` 是唯一的写入点，`debug_line_pass.cpp:226` 和
`deferred_lighting_pass.cpp:458` 都专门注释了「不计进去」）。`culled` 和它们凑成
「本来有多少 / 画了多少 / 剔了多少」才自洽。

阴影剔掉的数量另开一个字段 `shadow_culled`，这样「四级 cascade 省了多少」能被量出来 ——
没有这个数字，D5 那套做完之后无法证明它真的省了活。`FrameStatistics` 是纯聚合结构、
字段初始化，加字段不破坏调用方。

### 待定：抽取那边要不要做预剔除

把不可见的 draw 干脆不塞进 `RenderScene`（`render_scene_extractor.cpp`）比在 pass 里跳过更省 ——
省掉的是 `DrawItem` 的构造和 `world_bounds` 的八角变换。但那样 `culled` 就要由抽取来报，
而且拾取和阴影会拿不到被剔掉的 draw（阴影**需要**画面外的投射体，见 D5）。**倾向于不做**，
这一条留到拿到数字之后再判断。

---

## 任务清单

四个阶段。阶段 1 结束时有一个能过的单元测试但没有任何行为变化；阶段 2 之后 `culled` 开始动；
阶段 3 是省下四遍全场景遍历的那一步。

### 阶段 1 · `Frustum` 类型与测试设施

- [x] **1.1 打开 ArtiRenderer 的测试开关**（D2）
  - 文件：`CMakeLists.txt:47`
  - 做法：`ARTIRENDERER_BUILD_TESTS` 改成 `ON`（`SAMPLES` 保持 `OFF`），把 `:45` 那条注释
    跟着改成「tests 跟着建，samples 不建（要真窗口）」。
  - **改了 CMakeLists 就会触发 re-configure**，先按交接区那条把 PATH 补好。
  - 验收：`ctest` 从 3 条变 6 条（新增 `texture_desc_test` / `handle_test` / `aabb_test`），全绿。
  - **已完成。**re-configure 之后 6 条全绿。附带发现：`task_system_test` 里「parallelFor 应该
    被至少两个不同线程跑过」那条断言**偶发**会红（连跑 20 次复现 1 次）。跟这个开关无关，
    是那条断言本身对调度器抢占太敏感 —— 记在这里，不在本任务范围内修。

- [x] **1.2 `Frustum` 类型**（D1、D3）
  - 文件：新增 `ArtiRenderer/ArtiRenderer/include/frustum.h`（+ 需要的话 `src/frustum.cpp`）
  - 做法：`static Frustum fromViewProjection(const glm::mat4&)` 按 Gribb-Hartmann 取六个面并
    归一化；`bool intersects(const AABB&) const` 用「AABB 最正顶点」的保守测试。
    **`near` 面用 `row2`（ZO），不是 `row3 + row2`** —— 把 D3 那张表抄进注释，
    连「写成 NO 的症状是几乎什么都不剔」一起写。
  - 空 AABB（`isEmpty()`）约定为不可见，直接返回 false。
  - 验收：见 1.3。
  - **已完成。**纯头文件（`inline`，无 `.cpp`）—— 只有两个短函数，让调用点能内联，
    因为它马上要进每帧的 per-draw 循环。法线全部朝内，`intersects` 用「最内侧顶点」判据。
    另外：法线长度为 0 的平面直接跳过，这样默认构造的 `Frustum`（六个面全零）不剔任何东西，
    忘了初始化的症状是「没有剔除」而不是「画面全黑」。

- [x] **1.3 `frustum_test`**
  - 文件：新增 `ArtiRenderer/tests/frustum_test.cpp`，在 `tests/CMakeLists.txt` 注册
  - 做法：照 `aabb_test.cpp` 的形状（`test_check.h` + `ARTI_CHECK`，退出码即结果）。
    用 `glm::perspectiveRH_ZO` 建一个已知视锥，断言：
    1. 原点正前方的小盒子**可见**；
    2. 相机正后方的盒子被剔掉；
    3. 远平面之外的盒子被剔掉；
    4. 左 / 右 / 上 / 下各出一个明显在外面的盒子，都被剔掉；
    5. 横跨近平面的大盒子**可见**（保守测试不许剔掉部分相交的东西）；
    6. 空 AABB 不可见。
    7. **外加一组 `glm::orthoRH_ZO` 的用例**（这一条是做的时候补上的，见下面）。
  - 验收：`ctest` 7 条全绿。**并且反向验证一次**：把 near 面临时改成 NO 的公式
    （`row3 + row2`），必须有断言变红 —— 证明这个测试不是空转的。
  - **已完成，35 条断言全绿，反向验证成立。**过程里发现原计划有个空洞，值得记下来：
    - 第一版只有透视用例（20 条），反向验证**没有变红** —— 20/20 仍然全过。
      原因见 D3 那张表：透视下 NO 公式只是把近平面从 `-0.1` 挪到 `-0.05`，方向没变，
      「相机背后的盒子」照样剔得掉。**上面第 2 条断言抓不住它要抓的那个 bug。**
    - 于是补了正交那一组（9a–9f，15 条）：`orthoRH_ZO(±20, near=1, far=100)`，
      断言相机后方 `z≈+50` 和 `z∈[2,4]` 两个盒子都被剔、近平面到 `z=-1` 的距离正好是 0。
    - 重做反向验证：正交那 4 条红、透视一条没动 —— 跟诊断完全对上，防线成立。
    - 教训：反向验证要**用生产代码真正会走的那种矩阵**去做。阴影 cascade 走正交，
      光测透视等于没测。

### 阶段 2 · 相机剔除（G-Buffer + 拾取）

- [x] **2.1 可见集合进 `FrameContext`**（D7）
  - 文件：`src/pipeline/frame_context.h`、`frame_context.cpp`
  - 做法：构造时按 `scene.view.projection * scene.view.view` 建 `Frustum`，对每个 draw 算一个
    bool 存 `std::vector<bool>`（或 `vector<uint8_t>`），暴露 `bool isVisible(size_t) const`。
    **不在这里数 `culled`** —— 数字由 GBufferPass 报（D8），因为「有多少被剔」要和
    「有多少被画」在同一个循环里数才不会算重。
  - 验收：编译过；此时还没有消费者，行为不变。

- [x] **2.2 接到 `GBufferPass`**（D4）
  - 文件：`src/pipeline/passes/gbuffer_pass.cpp:266`（材质常量趟）、`:279`（绘制趟）
  - 做法：两趟都加 `if (!frame.isVisible(index)) continue;`。绘制趟里在跳过时
    `++frame.statistics().culled`；**材质常量那趟不要数**，否则一个 draw 会被数两次。
    循环要改成带下标的形式（现在是 range-for）。
  - 验收：见 2.4。

- [x] **2.3 接到 `PickingPass`**（D4）
  - 文件：`src/pipeline/passes/picking_pass.cpp:278`
  - 做法：同上加跳过，**不数 `culled`**（那是 G-Buffer 的职责，拾取 pass 不是每帧都跑）。
    把 `:284` 那条「和 GBufferPass 逐条对齐地跳过」的注释更新成「包括可见性」。
  - 验收：见 2.4。

- [x] **2.4 阶段 2 的验收**
  - `scene_editor` 打开一个场景，把相机转开让部分物体出画：播放器 / 编辑器的
    `%u culled` **不再是 0**，且 `draw_calls + culled` 等于场景里的 PBR submesh 总数。
  - **拾取一致性**：视锥边缘上点几下 —— 看得见的都点得到，点空处不选中东西。
    这是 D4 那条不变式的验收。
  - 相机转回来时 `culled` 回到 0（或接近），说明剔的是真在外面的东西。
  - **已完成。**执行者 2026-09-03 在 `scene_editor` 里看过：转相机 `culled` 变、和守恒、
    边缘拾取一致。编辑器状态栏为此加了 `culled` 一列（播放器 `--stats` 本来就有）。

### 阶段 3 · 阴影剔除（省下四遍全场景）

- [x] **3.1 `computeShadowCascades` 把每级的可见集合返回出来**（D5、D7）
  - 文件：`src/pipeline/shadow_cascades.h`、`shadow_cascades.cpp:158-165`
  - 做法：`ShadowCascadeResult` 加 `std::array<std::vector<uint8_t>, kShadowCascadeCount>
    caster_visible`（或一个打平的 bitset）。`:158` 那个已经在跑的 XY 重叠循环里，
    重叠时顺手把这一级的对应位置成 1。
    **`caster_bounds` 已经是光空间 AABB 了**（`:79-95`），不用新算。
  - 注释里必须写下 D5 那条依赖：**Z 方向之所以不用判，是因为 near/far 就是按 XY 重叠的
    投射体撑开的**（`:156-165`）。谁把 near/far 改成固定值，这条剔除就会剔掉合法投射体。
  - 验收：见 3.3。
  - **代码完成。**打平存成 `kShadowCascadeCount × draws.size()` 一个 vector，而不是四个
    vector —— 长度固定，展开也没多复杂。空盒单独置 1、不参与 near/far 收紧
    （修了一个预存在的坑）。

- [x] **3.2 接到 `ShadowPass`**
  - 文件：`src/pipeline/passes/shadow_pass.cpp:229`（外层 cascade 循环）、`:241`（内层 draw 循环）
  - 做法：内层加 `if (!computed.caster_visible[index][draw_index]) continue;`，跳过时
    `++frame.statistics().shadow_culled`。`renderer.h:45` 的 `FrameStatistics` 加
    `shadow_culled` 字段（D8）。
  - 验收：见 3.3。
  - **代码完成。**`FrameStatistics` 加了 `shadow_culled`；编辑器状态栏和播放器 `--stats`
    都显示。等 3.3 的画面比对。

- [x] **3.3 阶段 3 的验收 —— 必须有画面比对**
  - **`shadow_culled > 0`**（否则这一步等于没做），且四级总遍历数明显小于 `4 × submeshes`。
  - **画面不变**：这是唯一真正重要的验收。同一个场景、同一个相机位姿，剔除前后截图对比 ——
    阴影不许有任何变化。特别要构造一个「本体在画面外、影子在画面内」的情形（比如一个高塔
    在相机侧后方、影子横穿画面），确认那个影子**还在**。这一条专治 D5 写错的情况。
  - 相机转动时阴影边缘不许有物体的影子闪进闪出。
  - **已完成。**执行者 2026-09-03：`shadow culled` 是正数、影子不闪、本体出画影子还在。

### 阶段 4 · 文档与推指针

- [x] **4.1 文档**
  - 文件：原计划还要改 `ArtiRenderer/ArtiChoco/artichoco/renderer/README.md`，**没改** ——
    那一层是 Vulkan / NVRHI 边界，动它就要推三层指针，而剔除代码在 `ArtiRenderer/ArtiRenderer`。
    实际改的是引擎架构文档：
    - `docs/Architecture/Rendering.md` 第 8 节（`FrameStatistics`）+ 新增第 9 节（视锥剔除）
    - `docs/Architecture/README.md` 缺口表两行划掉、7.1 那行从「抽取」改成「FrameContext 构造」
    - `docs/Architecture/Scene.md` 「目前不做」里那条视锥剔除删掉，指向 Rendering.md
  - 做法：写清三个 pass 各用什么视锥（D4 那张表）、阴影为什么是 XY 重叠判据而不是相机视锥
    （D5）、以及 near/far 那条依赖。
  - 验收：一个没参与这次改动的人能只读文档说出「为什么阴影不能用相机视锥剔」。

- [x] **4.2 两层 submodule 推指针**
  - 做法：ArtiRenderer 提交 → ArtiEngine 推 ArtiRenderer 指针（`chore(deps)`）。
    **只有两层** —— 这次没动 ArtiChoco。
  - 验收：从干净克隆 `git submodule update --init --recursive` + `cmake --preset debug` +
    `cmake --build --preset debug` + `ctest` 全通。
  - **已完成。**代码指针在阶段 3 结束时已经推到 `4ba6e23`。阶段 4 只动引擎文档，不再动
    ArtiRenderer。`ctest` 7/7 在阶段 3 落地时跑过。

---

## 端到端验收

1. `cmake --preset debug` + `cmake --build --preset debug` 干净通过（无新增 warning）。
2. `ctest` 7 条全绿：原有 3 条（`task_system_test` / `physics_smoke` /
   `asset_pipeline_smoke`）+ D2 白捡的 3 条 + 新的 `frustum_test`。
3. `frustum_test` 的**正交**那组断言**反向验证过会失败**（把 near 面改成 NO 公式）。
   注意是正交那组 —— 透视那组反向验证不会红，理由见 D3。
4. `scene_editor` 里 `culled` 随相机转动变化，且 `draw_calls + culled` 守恒。
5. 视锥边缘的拾取一致：看得见就点得到。
6. `shadow_culled > 0`。
7. **阴影画面剔除前后逐像素一致**，包括「本体出画、影子在画面内」那个情形。
8. `arti_player projects/<项目>.artiproj` 正常跑，播放器 UI 上的 `culled` 有数字。

**不在验收范围内**：帧时间的具体数字。剔除**应该**更快，但这台机器是 RX 580、场景也小，
测出来的数字说明不了什么。真要量化收益，量的是「四级 cascade 的总遍历数」这个确定量
（`shadow_culled`），不是墙钟。

---

## 风险与注意

### 最大的风险是「看起来能跑但没生效」

D3 的 near 面写错、D5 的判据抄成相机视锥，两种错误都**不会崩、不会报错**：
第一种在正交下表现为 `culled` 几乎恒为 0（透视下则**完全看不出来**，只是近处多一条 0.05 的缝，
所以更阴），第二种表现为某些影子消失（而且只在特定相机角度下）。
所以 1.3 的反向验证和 3.3 的画面比对不是可选项 —— 它们是这个任务唯一的防线。

阶段 1 已经踩过一次这个坑的**弱化版**：反向验证本身也会「看起来做了但没生效」。
第一版反向验证跑出 20/20 全过，差一点就当成「测试有效」记账了。规矩定下来：
**反向验证必须真的看到红色，并且红的那条断言要和注入的错误对得上。**

### 两层 submodule，不是三层

改动几乎全在 `ArtiRenderer/ArtiRenderer`（ArtiRenderer submodule 自己的库）。
提交顺序：ArtiRenderer → ArtiEngine 推指针。**`ArtiRenderer/ArtiChoco` 这次不用动** ——
除了 4.1 要改 `artichoco/renderer/README.md`，那一条会让 ArtiChoco 也要提交 + 推指针，
变成三层。做 4.1 时留意这一点。

### 改 `CMakeLists.txt` 会弄坏 cmake 缓存

1.1 和 1.3 都改 CMake。见交接区那条 PATH 说明，**先补 PATH 再改**。

### `.clang-format` 和实际风格不符

别用 clang-format 格式化整个文件 —— 它会把函数大括号改成另一种风格，制造一堆无关 diff。
照周围代码手写。

### `projects/Assets/**/*.meta` 的 CRLF 噪声

工作区里那七个 `.meta` 的改动是历史遗留的换行/重写噪声，**不要 `git add .`**，
提交时逐个点文件。
