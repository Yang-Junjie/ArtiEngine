# ArtiChoco 的 JobSystem：把 TaskSystem 做成真的能用

| | |
| --- | --- |
| **状态** | 已完成 —— 第一层做完并验收。`examples/test_app` 已删（过时，example 另开项目） |
| **创建** | 2026-09-02 |
| **最后更新** | 2026-09-03 |
| **涉及仓库** | **ArtiChoco**（几乎全部改动）→ ArtiRenderer（推指针）→ ArtiEngine（推指针 + 架构文档）。三层 submodule，见「风险与注意」 |
| **目标** | 把 `arti::core::TaskSystem` 从「一个能跑但没人用、且有两处真 bug 的 enkiTS 薄壳」做成一个**能被依赖的 job system**：进程级生命周期、任务句柄、依赖图、优先级、grain size、pinned 任务、线程命名与 profiler 钩子，外加一套能证明它**真的在多线程跑**的 ctest |
| **明确不做** | 不接任何真实消费者（资产管线 / 剔除 / 物理 / 渲染线程都不动）。**用户拍板**：只做第一层，但要把第二层的接口留清楚 |

---

## 交接区

> **全文唯一允许改动的段落。每次收工前更新这里。**

**结论：第一层做完了。** `TaskSystem` 现在是进程级单例，有句柄、grain size、三档优先级、
pinned、外部线程、`TaskGraph`。核心验收（6.2）过了，而且反向验证过不是空转。没有接任何
真实消费者 —— 那是层二，接口表在架构文档 7.1。

**6.5 取消**：`examples/test_app` 整棵删掉。那个 example 过时了，example 另开项目写，
不在本仓库跟。`render_system.cpp` 换新 API 的那一半跟着一起走。

指针：ArtiChoco `bd6c171` → ArtiRenderer `cb01637`。还没 push。

**本任务的核心验收（6.2）已经过了，而且验证过它不是空转的**：把 `min_range` 临时改成
`kCount`（逼成一个分片）之后那条断言确实变红（「实际只有 1 个」），改回 512 又变绿。

开工前那件事已经办了：`vkGetDeviceProcAddr` 的改动单独提交在 ArtiChoco 的 `ab310fc`，
指针也一路推上来了（ArtiRenderer `fc266de`、ArtiEngine `3e25a8c`）。

### 先记一条构建环境的坑（撞过一次，代价是 cmake 缓存被弄坏）

**`ninja` 和 `clang` 都不在普通 shell 的 PATH 上。** 它们在 VS 18 Community 里：

```
/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja
/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin
```

平时 `cmake --build --preset debug` 能跑是因为它不需要重跑 configure。**一旦改了任何
`CMakeLists.txt`**，ninja 会触发 re-configure，那一步找不到 `ninja` 就报
`CMake was unable to find a build program corresponding to "Ninja"`，
并且把 `CMakeCache.txt` 里的 `CMAKE_C_COMPILER` / `CMAKE_CXX_COMPILER` 打回 `UNINITIALIZED`、
`CMAKE_MAKE_PROGRAM` 打成 `NOTFOUND`。**修法**：把上面两个目录 prepend 到 PATH，
重跑一次 `cmake --preset debug` 就能修好缓存（对象文件还在，不用全量重编）。
clang 22.1.3 能自己找到 MSVC 的头和库，**不需要先跑 vcvars64**。

### 阶段 1 的实际形状

- `TaskSystemConfig{ worker_count, external_thread_count, name_threads }`，
  `worker_count == 0` 时**不填** enkiTS 的 `numTaskThreadsToCreate`，保持它自己的默认。
- `init` / `shutdown` / `isInitialized` / `get()`（抛 `std::logic_error`）。构造和析构转私有，
  实例是 `task_system.cpp` 里的一个文件内静态裸指针，由 `init` / `shutdown` 管。
- 调用点：`entry_point.cpp` 里 `Logger::init()` 之后 `TaskSystem::init()`；收尾加了个
  `shutdownTaskSystem()`（照 `shutdownLogger()` 的 noexcept 形状），**排在 `shutdownLogger()`
  之前** —— worker 退出的路上还会打日志。`Tools/asset_tools/main.cpp` 和
  `Tools/asset_tools/tests/asset_pipeline_smoke.cpp` 各自在自己的 `Logger::init()` 旁边补了一对。
- `Application::m_task_system` 和那个前向声明都删了。

### 阶段 2 的实际形状，以及一个 D3 没写到的坑

`task/task_pool.{h,cpp}`（`arti::core::detail::TaskPool`）。`TaskHandle` 放在
`task_system.h` 里（它是公开 API 的一部分），池子的头 include 它，`TaskSystem` 用
`unique_ptr<detail::TaskPool>` 持有以打断循环包含。

**D3 漏了一条，实现时才发现，很值得记住**：enkiTS 是在 `AddTaskSetToPipe` 里才把
`m_RunningCount` 抬起来的，所以**任务在入队之前 `GetIsComplete()` 就是 `true`**。
如果 `insert()` 之后、`AddTaskSetToPipe()` 之前另一个线程触发了回收扫描，这个还没入队的任务
会被当成「已完成」回收掉，句柄立刻失效。所以槽位多了一个 `launched` 标记：

```
insert()（槽位 = pending，扫描跳过）→ AddTaskSetToPipe() → markLaunched()
```

代价是每次 submit 两次进出池子的锁。**没有走「持锁期间入队」那条路**：
`AddTaskSetToPipe` 在管道满时会**当场把任务跑掉**，那个任务要是又来 submit，
持着池子的锁就死锁了。

另外两处实现上的选择：

- **回收扫描只在空闲表空了才做**。每次 insert 都全扫的话，在途任务多时是 O(n²)。
- **`slotCount()` 通过 `TaskSystem::taskSlotCount()` 暴露给测试**。没有它，「泄漏修好了」
  这件事就只能靠读代码相信。

### 阶段 2 的验收

`ctest` 三个仍全绿（`task_system_test` 0.41s —— 涨的那 0.34s 就是十万次 submit/wait）。
三条新断言：

1. 十万次 submit + 逐个 wait，`taskSlotCount()` 结束时有上界（逐个等的话在途只有一个，
   所以实际就是 1）。这是旧 `m_pending` 无界增长的回归。
2. 陈旧句柄上 `wait` / `isComplete` 不崩、报完成。**这条测试自带一个防空转的断言**：
   先断言第二个任务确实复用了同一个槽位且世代号 +1，否则「陈旧句柄」根本没形成，
   后半个测试是白跑的。
3. `waitForAll()` 之后 256 个句柄全部报完成。

### 和文档不一样的四处（都记着理由）

1. **把 6.1（测试目标）提前到阶段 1 之后做了。** 照原计划测试排在最后，那意味着阶段 2～5 一共
   五个阶段的代码在落地时都没有任何断言看着 —— 而这个任务恰恰是「没人练它」出的问题。
   现在 `task_system_test` 已经在 `ctest` 里跑，后面每个阶段往里加断言。
2. **没有新增 `ARTICHOCO_BUILD_TESTS` 开关，用的是现成的 `BUILD_TESTING`。** 根
   `CMakeLists.txt` 的 `include(CTest)` 已经把它打开了，而 `asset_pipeline_smoke` 和
   `physics_smoke` 用的就是这个门（`if(BUILD_TESTING)`）。少一个开关，而且 D9 想要的效果
   （跟着 ArtiEngine 的 ctest 跑）自动成立。**代价**：ArtiChoco 单独构建时不 `include(CTest)`，
   所以那种构建里这个测试不建。
3. **1.4 的验收从「在调试器里看一眼」升级成了真断言。** 测试用
   `CreateToolhelp32Snapshot` + `OpenThread` + `GetThreadDescription` 从进程外面读线程名，
   正负两面都断（`name_threads` 开 → 找得到 `ArtiChoco-Worker-*`，关 → 找不到）。
   **之所以能做到确定性**：worker 是在 `init` 返回之前就起好名的，不依赖任务落在哪个线程上。
4. **1.2 的验收原文写的是 `workerCount()`，但那个函数要到 4.2 才有**，所以实际断的是
   `taskThreadCount() == worker_count + 1`。4.2 做完后把断言换成 `workerCount()`。

### 阶段 3 的实际形状

`TaskPriority::{High,Normal,Low}` → `TASK_PRIORITY_{HIGH,MED,LOW}`。`ParallelForOptions{min_range, priority}`。
阻塞版 `parallelFor` / `parallelForRanges` 用栈上 `TaskSet`（不进池）；异步版
`submitParallelFor` / `submitParallelForRanges` 走 `launch()`。`min_range == 0` 夹到 1。

**比清单多出来的 `parallelForRanges`**：逐元素那个是它的糖。层二的剔除 / 抽取要用
`thread_index` 做每线程 bucket，没有 range 回调就只能自己再切一遍。

### 阶段 3 的验收

`ctest` 三个仍全绿。新断言：`parallelFor` 每个下标写一次；`min_range = count` 只出一个
partition；`min_range = 0` 仍覆盖全部；`submitParallelFor` 拿句柄 wait 后结果正确；
三档优先级都能提交并完成（效果不断言，D5）。

### 阶段 4 的实际形状

- 旧 `pinned` / `launchPinned` / `waitForPinnedTask` / 单槽 `m_pinned_task` 删掉。
  `submitPinned` 走同一个池，`AddPinnedTask` 之后 `markLaunched`。
- `taskThreadCount()` 改名 `threadCount()`。`workerCount()` **不是** `threadCount()-1`：
  有 `external_thread_count` 时那个公式会把外部槽位算进去。实际返回
  `GetConfig().numTaskThreadsToCreate`。
- `threadIndex()` 转发 `GetThreadNum()`；未注册线程是 `kNoThread`（= enki 的
  `NO_THREAD_NUM`，头文件不 include enkiTS）。
- `registerExternalThread` / `deregisterExternalThread` 原样转发。
- **6.5 提前做了一半**：`examples/test_app/render_system.{h,cpp}` 的 `launchPinned` /
  `waitForPinnedTask` 换成了 `submitPinned` + `wait`。旧 API 已经不在了，不换它编不过。
  `verifyTaskSystem()` 那个「4096 次乘 2」还留着，阶段 6 再改活量。

enkiTS 的线程编号：0 = 调用线程；`[1, 1+external)` = 外部槽位；再往后才是它自己建的
worker。所以 `submitPinned(1, ...)` 在 `external_thread_count == 0` 时钉的是第一个
worker，有外部槽位时钉的是第一个外部槽。`test_app` 现在没留外部槽，钉 1 仍然是 worker。

### 阶段 4 的验收

`ctest` 三个仍全绿（`task_system_test` 0.43s）。新断言：`submitPinned(1)` 函数体里
`threadIndex()==1`；连续两次 `submitPinned(1)` 都跑到（旧单槽 UAF 的回归）；
`submitPinned(0)` 落在调用线程（`WaitforTask` 会顺手跑本线程 pinned 队列，不需要单独泵）；
`std::thread` 注册后能 `submit`/`wait`，注销回到 `kNoThread`；没留槽位时注册失败；
有外部槽位时 `workerCount()` 仍是配置的 worker 数。

### 阶段 5 的实际形状

`task/task_graph.{h,cpp}`。`TaskGraphNode` 只是图内下标，和 `TaskHandle` 刻意不同型。
`add` / `addAfter` / `addParallelFor` / `addParallelForAfter` / `addPinned` / `addPinnedAfter`，
提交入口是 `TaskSystem::submit(TaskGraph&&)`。

- **整张图 = 一个池槽位。** `TaskGraph::Storage` 自己就**继承** `enki::ICompletable`，
  它同时是「存储体」和「隐式终结节点」。省掉一层间接，而且池子里那个
  `shared_ptr<ICompletable>` 指的就是终结节点 —— `wait(h)` 直接等它。
- **前驱只能是已经建过的节点**（`pred.index >= node.index` 就抛）。这顺手把自环和后向边
  挡在建图阶段，代价是不支持「先声明后填」的建图顺序 —— 现在没有消费者需要那个。
- **`Dependency` 存在节点里、用 `vector` 且建图期间不删**。它是侵入式链表节点，
  移动会改前驱的 `m_pDependents`，所以只 `emplace_back` + `SetDependency`，不 erase。
- 提交顺序：**先校验（pinned 下标越界、非空图但没有根）→ 连终结边 → 入池 → 只入队根节点
  → `markLaunched`**。校验放在入池之前，抛出去的时候不会留下一个占着的槽位。

**空图是合法的**：终结节点没有依赖 → `m_RunningCount` 是 0 → `GetIsComplete()` 直接为真，
`wait()` 立刻返回。

### 阶段 5 的验收

`ctest` 三个仍全绿。三条新断言：建图不 submit 时什么都不跑；菱形 `A → {B,C} → D` 的顺序
正确且 D 只跑一次（用原子标志断言，没有 sleep）；`decode(worker) → upload(pinned 1)`
两节点图跑通且 upload 体内 `threadIndex() == 1` —— 这就是层二「解码 + 上传」那一行的形状。

### 阶段 6.3 / 6.4 的实际形状

- **6.3**：`testShutdownWithTasksInFlight` —— 512 个故意不 wait 的 `submit`，然后 `shutdown()`，
  断言全跑完。这是 `~TaskSystem` 里 `WaitforAllAndShutdown()` 排在成员析构之前的回归
  （enkiTS 的 `~ICompletable` 有 `GetIsComplete()` 断言）。TSan 没跑：
  `clang++ -fsanitize=thread` 在 `x86_64-pc-windows-msvc` 上报 `unsupported option`。
- **6.4**：完整文档写在 `artichoco/core/task/README.md`（跟 `asset/` `scene/` `renderer/`
  一样按模块放）。根 `ArtiChoco/README.md` 原先是 0 字节空壳，补了一段入口 + 解码/上传
  两节点图，而不是凭空编一份总览。架构文档第 7 节那一行改名成「多线程的消费者」，
  层二那张表作为 7.1 搬过去。

### 6.5 只能做到一半 —— `test_app` 在本任务开工前就编不过

`ARTICHOCO_BUILD_EXAMPLES=ON` 单独配一次之后（`cmake -S . -B <dir> -G Ninja -DCMAKE_BUILD_TYPE=Debug
-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DARTICHOCO_BUILD_EXAMPLES=ON`）：

- **`render_system.cpp` 编过了** —— 这是本任务改的那个文件（`launchPinned` / `waitForPinnedTask`
  换成 `submitPinned` + `wait`），也是 6.5 真正要验的东西。
- **`test_app_layer.cpp` 三个错误，与本任务无关**，是 NVRHI 迁移（`9287849`）时 example 没跟上：
  - `no member named 'nvrhiResourceSmoke' in 'RenderDevice'`（:336）
  - `no member named 'nvrhiComputeShaderSmoke' in 'RenderDevice'`（:339）
  - `invalid argument type 'RenderFrameResult' to unary expression`（:699）—— `renderFrame()`
    现在返回结构体（`render_device.h:43,75`），调用点还在 `if (!...)`。
  前两个函数只存在于 `render_device.cpp` 的 Impl 里（`:55-56`），公开头上没有。

**证据**：这三处调用在 `ab310fc`（本任务第一个提交之前）里一字不差地存在。所以
`test_app` 目标整体编不过**不是这次改出来的**，也不该由这个任务顺手修 —— 那是渲染器
API 漂移，够单独一个任务。`verifyTaskSystem()` 的活量（现状第 11 条）也因此没动。

### 待定项已定

线程命名用 `#if defined(_WIN32)` 写在 `task/thread_naming.cpp` 里。这其实**不算偏离约定** ——
`artichoco/core/io/paths.cpp` 早就是这么干的（同样的 `WIN32_LEAN_AND_MEAN` / `NOMINMAX` /
`windows.h` 三连）。「在 CMake 层按平台选源文件」那条是 `Tools/platform` 的约定，针对的是
一整组平台接口，不是两行诊断代码。

### 阶段 1 的验收都过了

- `cmake --build --preset debug` 干净（21 个目标，无新增 warning）。
- `ctest` 三个全绿：`task_system_test` 0.07s、`physics_smoke` 0.01s、`asset_pipeline_smoke` 0.60s。
- **`asset_tools list` 的日志第一行就是 `TaskSystem initialized: 6 thread(s) including the
  calling thread`，结尾是 `TaskSystem shutdown finished`** —— 现状第 2 条（CLI 进程里
  `get()` 是 `*nullptr`）由此修掉了，这是实测不是推理。
- `arti_player` 和 `scene_editor` 各跑 6 秒，日志里 init 那行都在。

已确认的事实（都是读代码 / 跑命令得出的，不是从文档抄的），详见「背景与现状」：

- `TaskSystem` 在 `artichoco/core/task/task_system.{h,cpp}`，`.cpp` 只有 91 行。
- **全工程没有一个真实消费者。** 只有 `artichoco/core/application.cpp:120` 建它，
  `examples/test_app` 用它自测。而 `ARTICHOCO_BUILD_EXAMPLES` 默认 **OFF**，ArtiEngine 也没开 ——
  所以那个自测在本项目的构建里**根本没编译过**。
- `asset_tools` 有自己的 `main`（`Tools/asset_tools/main.cpp:332`）、没有 `Application`，
  所以那个进程里 `TaskSystem::get()` 是 `*nullptr`。
- ArtiChoco **一个 ctest 目标都没有**（`add_test` 在它的 CMake 里搜不到），也没有 tests 目录。
  本任务要新建第一个。

---

## 背景与现状

### 现在有什么

`ArtiChoco::Core` 里有一个 `TaskSystem`（`artichoco/core/task/task_system.h`），enkiTS 的薄封装，
由 `Application` 持有（`application.h:54` 的 `std::unique_ptr<TaskSystem> m_task_system`），
在 `Application::init()` 里建（`application.cpp:120`）、`shutdown()` 里拆（`:131`）。

公开 API 一共这些：

```cpp
static TaskSystem& get();
template <typename Fn> void parallelFor(uint32_t count, Fn&&);   // 阻塞
template <typename Fn> void submit(Fn&&);                        // 发射后不管
template <typename Fn> void pinned(uint32_t thread_index, Fn&&); // 阻塞
void launchPinned(uint32_t thread_index, const std::function<void()>&);
void waitForPinnedTask();
uint32_t taskThreadCount() const noexcept;
void waitForAll();
```

### 为什么它不算「真正意义上的多线程能力」

按严重程度排，每条都标了出处。前两条是「有没有用」的问题，3 之后是「能不能用」的问题。

**1 · 没有一个真实消费者。** 对 `TaskSystem` / `parallelFor` / `submit` / `pinned` 搜整个仓库
（含 ArtiRenderer、ArtiChoco 的 asset / scene / renderer 三层、ArtiEngine、Tools、Runtime），
命中的只有：

| 位置 | 干什么 |
| --- | --- |
| `artichoco/core/application.{h,cpp}` | 建它、拆它 |
| `examples/test_app/test_app_layer.cpp:638` | `verifyTaskSystem()`：4096 次「乘 2」+ 32 个原子加 |
| `examples/test_app/render_system.cpp:40-50` | `launchPinned(1, ...)` 跑了个渲染线程 |

而 `ARTICHOCO_BUILD_EXAMPLES` 在 `ArtiChoco/CMakeLists.txt:5` 默认 **OFF**，ArtiEngine 的根
`CMakeLists.txt` 也没打开它 —— **所以连这个自测都没在本项目的构建里编译过**。资产 reconcile
是「全程同步单线程」、物理 `workerCount` 写死 1、渲染明确单线程。结论：这一层目前只在架构表格里
存在。

**2 · `asset_tools` 进程里它压根不存在。** 生命周期绑在 `Application` 上，而
`Tools/asset_tools/main.cpp:332` 有自己的 `main()`、自己调 `Logger::init()`、从不建
`Application`（Assets.md 也是这么写的：这一层「被 CLI 消费，那里根本没有 `Application`」）。
于是**最该并行的那件事（导入 / reconcile）所在的进程里 `TaskSystem::get()` 是 `*nullptr`**。
`Tools/asset_tools/tests/asset_pipeline_smoke.cpp:781` 同理。

**3 · `get()` 不判空**（`task_system.cpp:25-28` 直接 `return *s_instance;`）。项目自己的约定是
反的 —— Assets.md 写着 `AssetRuntime` 未打开时取 `manager()` 抛 `std::logic_error`，理由是
「比让调用方拿一个空引用再解引用更早也更明确」。

**4 · `submit()` 是设计上的泄漏。** 每次 submit 往 `m_pending` 塞一个 `unique_ptr<TaskSet>`
（`:43-50`），**只有** `waitForAll()` 会清（`:79-89`）。没人调 `waitForAll()` 就无界增长，
而且已完成的任务也不会被回收。

**5 · `submit()` 不给句柄。** 没法等某一个任务、问它完了没、或者拿结果。要么 `waitForAll()`
全等，要么什么都不等。

**6 · 依赖关系一点没暴露。** enkiTS 有 `Dependency` / `SetDependency` / `SetDependenciesArr`
（`third_party/enkiTS/src/TaskScheduler.h:119-125`、`:231-249`），能表达「A 和 B 都完了再跑 C」
—— 那正是资产导入的拓扑序、以及解码→上传串联需要的东西。包装层一个都没转出来。

**7 · `launchPinned` 只有一个槽位，是 use-after-free。** `m_pinned_task` 是单个 `unique_ptr`
（`task_system.h:60`），第二次调用直接覆盖（`:63`），而被析构的那个对象可能还挂在 scheduler 的
侵入式链表里（`IPinnedTask::pNext`，`TaskScheduler.h:200`）。

**8 · `pinnedImpl` 的调用顺序可疑**（`:53-59`）：`AddPinnedTask` → `WaitforTask` →
`RunPinnedTasks()`。`RunPinnedTasks` 排在 wait **之后**，等于「等它完成，然后再跑一遍本线程的
pinned 队列」—— 要么多余，要么在 `thread_index == 当前线程` 时是个卡死的写法。

**9 · `Initialize()` 不带配置**（`:15`），于是 `TaskSchedulerConfig`
（`TaskScheduler.h:278-294`）提供的东西全部没用上：

- `numTaskThreadsToCreate` —— 线程数只能是 `hardware_concurrency - 1`，没法给渲染线程留核。
- `numExternalTaskThreads` + `RegisterExternalTaskThread()`（`:401-409`）—— 让主线程 / 渲染线程
  **参与**任务系统的唯一入口。将来做渲染线程必须用它。
- `profilerCallbacks`（`:251-263`）—— 8 个回调（线程起停 + 四种 wait 的起止）。这是给线程命名和
  接 profiler 的**唯一**钩子。现在所有 worker 在调试器和 profiler 里都是无名的。
- `customAllocator`（`:265-275`）。

**10 · 五档优先级没暴露**（`TASK_PRIORITY_HIGH..LOW`，`:88-103`）。「这一帧要用的」和「后台流式
加载」应该分档，否则后台任务会挤掉帧内任务。

**11 · `parallelFor` 没有 grain size。** enkiTS 的 `m_MinRange`（`:171-180`，注释建议每个 partition
至少 ~10k 时钟周期以摊掉调度开销）没有出口。`verifyTaskSystem()` 里那个「4096 次乘 2」正是反面
教材：每次迭代几个周期，那测的纯粹是调度开销。

顺带一条命名问题：`taskThreadCount()` 返回 `GetNumTaskThreads()`，而按
`TaskScheduler.h:384-386` 它等于 `numTaskThreadsToCreate + numExternalTaskThreads + 1`
—— **包含调用线程**。`render_system.cpp:40` 那句 `< 2` 的判断因此是对的，但这个名字读起来像
「worker 数量」，容易被下一个人用错。

### 这次不做第二层 —— 以及它带来的风险

用户拍板：**只做第一层**（job system 本身），不接任何真实消费者，但要把第二层的接口留清楚。

这里必须把风险写下来，因为它正是现在这个 `TaskSystem` 的病因：**一个没有消费者的 job system
会长成「看起来合理但用起来不趁手」的形状，而且没人会发现。** 上面第 1、2、7、8 条能存在这么久，
就是因为没人真用过它。

本任务用两件事对冲：

1. **测试就是替身消费者**，而且验收里有一条专门证明「真的跑在多个线程上」——
   一个足够大的 `parallelFor` 必须被**至少两个不同的 `threadnum_`** 执行过。否则一个偷偷
   全在调用线程上跑的实现也能把所有 correctness 断言过掉。
2. **「层二的接口」那一节**（见下）把每个未来消费者、它现在的位置、以及它将会调哪个 API
   写成一张表。写不出来的 API 就说明这一层还缺东西 —— 这是本任务的设计自检。

---

## 设计决定

### D1 · 只做第一层，不接消费者 —— 已定（用户拍板）

我提过反对意见（没有消费者的 job system 会重复现在这个的命运），用户明确要求只做第一层、把接口
留清楚。**这是决定，不要再回头改范围。** 对冲手段见上一节。

### D2 · `TaskSystem` 变成进程级，照 `Logger` 的先例 —— 已定

现在它绑在 `Application` 上，于是 CLI 里不存在（现状第 2 条）。改成和 `Logger` 一模一样的形状：

```cpp
static void init(const TaskSystemConfig& config = {});
static void shutdown();
static bool isInitialized();
static TaskSystem& get();          // 未 init 时抛 std::logic_error
```

先例就在 `artichoco/core/log.h:77-80`（`init` / `shutdown` / `isInitialized`），调用点是
`entry_point.cpp:47` 的 `Logger::init()` —— 在 `createApplication()` **之前**无条件跑。
`TaskSystem::init()` 加在它后面一行，`shutdown()` 加在 `shutdownLogger()` 之前
（日志要活到最后，所以任务系统先关）。

`Application::m_task_system` 整个删掉（`application.h:15,54`、`application.cpp:4,120,131`）。
`asset_tools/main.cpp` 和 `asset_pipeline_smoke.cpp` 各自在 `Logger::init()` 旁边补一行
`TaskSystem::init()` —— 和它们已经在做的事完全同构。

**为什么不做惰性初始化**（第一次 `get()` 时自动建）：线程数、外部线程数这些是**进程启动时的
决定**，惰性建意味着谁先碰它谁定配置。`Logger` 也不惰性（`ensureInitialized()` 只是兜底断言）。

### D3 · 句柄 = 槽位下标 + 世代号；任务对象用 `shared_ptr` —— 已定

```cpp
struct TaskHandle {
    uint32_t index      = kInvalidIndex;
    uint32_t generation = 0;
    bool valid() const noexcept;
};
```

池子里一个槽位持 `std::shared_ptr<enki::ICompletable>` + 一个世代号 + 一个空闲标记。

- **回收时机**：`allocate()` 里顺手扫一遍在用的槽位，`GetIsComplete()` 为真的就还给空闲表、
  世代号 +1。不用 enkiTS 的 completion action —— 那会为「回收」这件事额外排一个任务。
- **陈旧句柄是安全的**：世代号不匹配 → `isComplete()` 返回 `true`、`wait()` 是空操作
  （语义：那个任务早就完事了）。32 位世代号，回绕不现实。
- **为什么是 `shared_ptr` 而不是 `unique_ptr`**：`wait()` 必须在**不持锁**的情况下调
  `WaitforTask`（它会顺便跑别的任务，持锁进去就是给死锁递刀）。持锁期间把 `shared_ptr` 拷一份
  出来再解锁，槽位就算同时被回收，对象也活到等待者用完为止。代价是每个任务一个控制块 ——
  真嫌重的话接缝是给它换个池分配器，但那要先有 profiler 数据。

这一条同时修掉现状的第 4 和第 5 条。

### D4 · 依赖用「先建图、后提交」的 `TaskGraph`，**不做** `then(运行中的句柄)` —— 已定

这不是风格选择，是 enkiTS 的硬约束。读 `TaskScheduler.h:109-142` 和 `:231-249`：

- `Dependency` 对象是**依赖方**的成员，`SetDependency` 把依赖方登记进被依赖方的
  `m_pDependents` 侵入式链表。
- 依赖方**不由调用者入队**，它是在所有前驱完成时由 `OnDependenciesComplete` 自动launch的。
- 所以依赖边**必须在前驱入队之前**连好。对一个已经在跑（甚至已经完成）的任务调 `SetDependency`
  是错的：前驱已经完成的话，依赖方永远不会被启动。

于是 API 只能是「攒一张图，再整体提交」：

```cpp
TaskGraph graph;
auto load  = graph.add([]{ /* ... */ });
auto parse = graph.addAfter({load}, []{ /* ... */ });
auto a     = graph.addParallelFor(count, [](uint32_t i){ /* ... */ });
auto sink  = graph.addAfter({parse, a}, []{ /* ... */ });
TaskHandle h = task_system.submit(std::move(graph));   // 整张图一个句柄
task_system.wait(h);
```

两个实现要点：

- 图自带一个**隐式终结节点**，依赖所有出度为 0 的节点。`wait(h)` 等的就是它，所以「等整张图」
  不需要调用方自己收集叶子。
- **整张图 = 一个池槽位**，槽位持的 `shared_ptr` 指向一个持有全部节点 + 全部 `Dependency`
  对象的存储体。这样 D3 的回收规则不会把「还有依赖方指着的前驱」提前释放掉 ——
  图内的生命周期是一个整体。

### D5 · 优先级只暴露三档 —— 已定

```cpp
enum class TaskPriority : uint8_t { High, Normal, Low };   // → HIGH / MED / LOW
```

enkiTS 有五档（`:88-103`），中间那两档（`MED_HI` / `MED_LO`）现在没有能说清楚的用途，暴露出来
只会让调用方在「到底该填哪个」上纠结。**留下的接缝**：`WaitforTask` 的第二个参数
`priorityOfLowestToRun_`（`:354`）才是让优先级真正生效的那一半 —— 高优先级的等待不会去跑低优先级
的活。这次不暴露它，等真有帧内 / 后台之分的消费者时再加。

### D6 · `parallelFor` 暴露 grain size —— 已定

```cpp
struct ParallelForOptions {
    uint32_t     min_range = 1;                    // enki 的 m_MinRange
    TaskPriority priority  = TaskPriority::Normal;
};
```

默认 1 是为了跟现在的行为一致；文档里写明「每个 partition 的活少于 ~10k 周期就该往上调」，
并把 `TaskScheduler.h:171-180` 那段注释的结论引过来。这条修现状第 11 条。

### D7 · 线程命名 + profiler 回调现在就做 —— 已定

`profilerCallbacks` 是纯函数指针、没有 userData（`:252`），所以要走一个文件内静态指针回到实例。
`threadStart` 里给 OS 线程起名（`ArtiChoco-Worker-<n>`）。

**为什么现在做而不是等 profiler**：这是唯一的钩子，位置在 `Initialize(config)` 那一次调用里 ——
后补要动同一处代码；而且线程有名字之后，光靠调试器的线程窗口就能看出「活到底有没有分出去」，
这对一个没有真实消费者的任务来说是最便宜的可观测性。

### D8 · pinned 任务改成多槽位，并区分「一次性」和「长驻」—— 已定

现状第 7 条是真 bug。改成：

```cpp
TaskHandle submitPinned(uint32_t thread_index, Fn&&);   // 一次性，走同一个池
void       runPinnedTasks();                            // 某个线程主动排空自己的 pinned 队列
uint32_t   threadIndex() const noexcept;                // GetThreadNum()，未注册线程返回 kNoThread
uint32_t   threadCount() const noexcept;                // 含调用线程，见下
uint32_t   workerCount() const noexcept;                // threadCount() - 1
```

- **长驻 pinned 任务**（渲染线程那种「进去就不出来」的循环）就是一个普通的 `submitPinned` +
  一直不 `wait` 的句柄。不需要 `launchPinned` / `waitForPinnedTask` 这对特例 API，删掉它们；
  等价写法是 `auto h = submitPinned(1, loop);` … `wait(h);`。
- `taskThreadCount()` **改名**成 `threadCount()`，并补一个 `workerCount()`。理由见现状末尾那条
  命名问题：`GetNumTaskThreads()` 含调用线程，旧名字读起来像不含。
- 不再复制 `pinnedImpl` 那个「wait 完再 `RunPinnedTasks`」的顺序（现状第 8 条）。
  `runPinnedTasks()` 单独暴露，由需要排空自己队列的线程自己调。

### D9 · 测试跟着 ArtiEngine 的 `ctest` 一起跑 —— 已定

ArtiChoco 现在**一个 ctest 目标都没有**。本任务新建 `artichoco/core/tests/task_system_test.cpp`
和一个 `ARTICHOCO_BUILD_TESTS` 开关（**默认 ON**），照 `Tools/asset_tools/CMakeLists.txt:32`
（`add_test(NAME asset_pipeline_smoke COMMAND asset_pipeline_smoke)`）那个形状写 —— 项目里的测试
就是「一个带 `main()` 的可执行 + `add_test`」，不引入测试框架。

**这里刻意偏离了一个先例**：ArtiEngine 的根 `CMakeLists.txt:46-47` 把
`ARTIRENDERER_BUILD_SAMPLES` / `ARTIRENDERER_BUILD_TESTS` 强制 OFF（「被当依赖库消费，它自己的
samples 和 tests 不需要跟着建」）。ArtiChoco 的这个开关**不要**跟着关掉，理由：

1. 这个测试纯 CPU，不开窗、不碰 GPU、不读资产，代价接近于零。
2. 本任务最大的风险就是「没人练它」（D1）。让它每次 `ctest` 都跑是唯一的对冲。
3. ArtiEngine 自己的 `ctest` 已经有 `physics_smoke` 和 `asset_pipeline_smoke`，再多一个同性质的
   目标是一致的。

真要改回一致，就是在根 `CMakeLists.txt` 里加一行 force OFF —— 但那等于把这一层的验证扔掉。

### D10 · 明确不做的四件事 —— 已定

| 不做 | 理由 | 留下的接缝 |
| --- | --- | --- |
| `Future<T>`（带返回值的任务） | 现在没有消费者需要，泛型返回值会把 API 面积翻一倍 | 句柄 + 调用方自己捕获输出变量（`parallelFor` 的每线程 bucket 用 `threadIndex()`） |
| 任务取消 | enkiTS 不支持，硬做要在每个任务体里插检查点 | 调用方传一个 `std::atomic<bool>` 令牌自己查；enkiTS 自己也是这个路子（`GetIsShutdownRequested()`，`:326`） |
| 渲染线程迁移 | 那是帧数据 double buffer、ImGui 线程归属、swapchain 重建时机三件事，比本任务大 | `external_thread_count` + `registerExternalThread()` + 长驻 `submitPinned` |
| 自己写 work-stealing / fiber | enkiTS 就是干这个的，而且已经在依赖里 | 无 —— 真要换调度器，`TaskSystem` 这一层的存在意义正是让它可换 |

### 待定：线程命名的实现放哪

`SetThreadDescription`（Windows）/ `pthread_setname_np`（其他）是两行代码。项目在
`Tools/platform` 那里的约定是**在 CMake 层按平台选源文件，实现里不带 `#ifdef`**
（Applications.md 第 2 节）。为两行代码建一套 CMake 分派我觉得不成比例，**倾向于**
一个 `task/thread_naming.cpp` 里带 `#ifdef _WIN32`。开工时确认一下这个偏离能不能接受。

---

## 层二的接口（本任务的交付物之一）

用户的要求是「确保容易接入第二层，留下明确的接口」。所以下面这张表是**验收内容**，不是随笔：
每一行都是一个已经能指出位置的未来消费者，以及它将会调的 API。**如果某一行写不出对应的 API，
说明这一层还缺东西，那就是设计漏洞而不是「以后再说」。**

| 未来消费者 | 现在在哪 | 会调什么 | 现在有的证据 |
| --- | --- | --- | --- |
| 资产 reconcile 的 scan | `AssetPipeline::planReconcile()` / `scan()` | `parallelFor(files, fn, {min_range})` | 架构 README 第 7 节：「`scan()` 是纯读、无共享写，将来换 `parallelFor` 语义不变」 |
| 资产导入的拓扑序 | `AssetPipeline::reconcile()` | `TaskGraph`：一个源文件一个节点，依赖边就是拓扑序 | ArtiChoco `asset/README.md` 里的 prescan / 推断 / 拓扑序 |
| 纹理 / 网格解码 + 上传 | `GPUAssetCache` | `submit`（解码，worker）+ `submitPinned`（上传，渲染线程）+ `TaskGraph` 串起来 | Rendering.md 第 1 节已经把界限划好了：「工作线程可以做文件 IO、解压、图片解码，然后把数据交给渲染线程上传」 |
| 视锥剔除 / 抽取 | `RenderSceneExtractor::extract()` | `parallelFor` + `threadIndex()` 做每线程 bucket | `DrawItem::world_bounds` 每帧已经算好；enkiTS 的 `threadnum_` 注释（`TaskScheduler.h:164-165`）明说这就是它的用途 |
| 物理多线程 | `PhysicsSystem` → Box3D 的 `b3EnqueueTaskCallback` | `submitParallelFor(count, fn, {min_range})` 拿句柄 + `wait(handle)` | Box3D 的任务回调要的正是「异步 parallel-for 返回一个可等待的东西」这个形状 |
| 渲染线程 | 三个 exe 的 layer | `TaskSystemConfig::external_thread_count` + `registerExternalThread()` + 长驻 `submitPinned` | API 已到位；原先演示形状的 `examples/test_app` 已删 |

从这张表反推，第一层**必须**有的东西（也就是任务清单的内容）：

```cpp
// 阻塞的 fork-join
template <typename Fn> void parallelFor(uint32_t count, Fn&&, ParallelForOptions = {});

// 异步 + 句柄
template <typename Fn> TaskHandle submit(Fn&&, TaskPriority = TaskPriority::Normal);
template <typename Fn> TaskHandle submitParallelFor(uint32_t count, Fn&&, ParallelForOptions = {});
template <typename Fn> TaskHandle submitPinned(uint32_t thread_index, Fn&&);

void wait(TaskHandle);
bool isComplete(TaskHandle) const;
void waitForAll();                      // 屏障 / 关停用，不是通用同步手段

// 依赖
TaskHandle submit(TaskGraph&&);

// 线程
uint32_t threadIndex() const noexcept;  // 每线程 bucket 的下标
uint32_t threadCount() const noexcept;
uint32_t workerCount() const noexcept;
void     runPinnedTasks();
bool     registerExternalThread();
void     deregisterExternalThread();
```

`waitForAll()` 要在文档里写清限制：enkiTS 自己的注释（`TaskScheduler.h:356-357`）说它
「not guaranteed to work unless we know we are in a situation where tasks aren't being continuously
added」。所以它是**关停 / 帧屏障**用的，不是「等我关心的那批活」的手段 —— 那个用句柄。

---

## 任务清单

六个阶段。**阶段 1 结束时行为完全不变**（只搬生命周期），阶段 2 修掉两个真 bug，
阶段 5 结束时上面那张表的 API 全部到位。

### 阶段 1 · 生命周期与配置（行为不变）

- [x] **1.1 `TaskSystem` 改成进程级，`get()` 判空**
  - 文件：`artichoco/core/task/task_system.{h,cpp}`
  - 做法：加 `init(const TaskSystemConfig&)` / `shutdown()` / `isInitialized()`，
    照 `artichoco/core/log.h:77-80` 的形状。`get()` 在未 init 时抛
    `std::logic_error("TaskSystem is not initialized")`。构造 / 析构转成私有，
    实例由 `init` / `shutdown` 管（一个文件内静态 `unique_ptr`）。
  - 验收：编译过；`get()` 在未 init 时抛而不是崩（阶段 6 的测试会正式断言这条）。

- [x] **1.2 `TaskSystemConfig` 与 `Initialize(config)`**
  - 文件：同上
  - 做法：`struct TaskSystemConfig { uint32_t worker_count = 0; uint32_t external_thread_count = 0;
    bool name_threads = true; }`。`worker_count == 0` 时**不填** `numTaskThreadsToCreate`，
    让 enkiTS 用它自己的默认（`hardware_concurrency - 1`）—— 别自己算，那是重复它的策略。
  - 验收：`worker_count = 1` / `= 4` 各 init 一次，`workerCount()` 报的数对得上。

- [x] **1.3 挪调用点**
  - 文件：`artichoco/core/entry_point.cpp`（`Logger::init()` 之后加 `TaskSystem::init()`，
    `shutdownLogger()` **之前**加 `TaskSystem::shutdown()`）、
    `artichoco/core/application.{h,cpp}`（删 `m_task_system` 和那个前向声明）、
    `Tools/asset_tools/main.cpp:332` 附近、`Tools/asset_tools/tests/asset_pipeline_smoke.cpp:781` 附近
  - 做法：CLI 那两处在各自的 `Logger::init()` 旁边补一行 init、结尾补 shutdown。
  - 注意：任务系统**先**关、日志**后**关（worker 退出时可能还要打日志）。
  - 验收：`scene_editor` / `arti_player` / `asset_tools` 三个都能起来；
    `ctest` 里 `physics_smoke` 和 `asset_pipeline_smoke` 仍绿。
    `asset_tools list <项目>` 里 `TaskSystem::isInitialized()` 为真（临时加一行 debug 日志确认，
    确认完删掉）。

- [x] **1.4 线程命名 + profiler 回调**
  - 文件：`artichoco/core/task/thread_naming.{h,cpp}`（新建，见「待定」那条）、`task_system.cpp`
  - 做法：`TaskSchedulerConfig::profilerCallbacks.threadStart` 里给线程起名
    `ArtiChoco-Worker-<n>`（`n` 是回调给的 `threadnum_`）。其余七个回调先接成空实现 + 注释写明
    它们各自对应什么（将来接 profiler 就在这儿）。回调是裸函数指针、没有 userData，
    所以走文件内静态指针。
  - 验收：调试器的线程窗口里能看到带名字的 worker，数量 = `workerCount()`。

### 阶段 2 · 句柄与回收（修掉泄漏和悬垂）

- [x] **2.1 槽位池**
  - 文件：`artichoco/core/task/task_pool.{h,cpp}`（新建，只在 `task_system.cpp` 里用）
  - 做法：按 D3 —— `std::vector<Slot>` + 空闲表 + 一把 `std::mutex`；
    `Slot { std::shared_ptr<enki::ICompletable> task; uint32_t generation; bool in_use; }`。
    `allocate()` 先扫一遍在用槽位回收已完成的（世代号 +1），再取空闲槽或增长。
  - 验收：单元测试里连续 allocate / 等待 / allocate 十万次，`slotCount()` 有上界。

- [x] **2.2 `submit` 返回句柄，删掉 `m_pending`**
  - 文件：`task_system.{h,cpp}`
  - 做法：`submit(Fn&&, TaskPriority)` → 建 `shared_ptr<enki::TaskSet>`、设
    `m_Priority`、进池拿句柄、`AddTaskSetToPipe`。**`m_pending` 和它那把
    `m_pending_mutex` 整个删掉。**
  - 验收：现状第 4 条那个泄漏不复存在（2.1 的上界测试就是证明）。

- [x] **2.3 `wait` / `isComplete`**
  - 文件：同上
  - 做法：**先持锁校验世代号并把 `shared_ptr` 拷出来，解锁，然后**才调
    `m_scheduler->WaitforTask(ptr.get())`。持锁调 `WaitforTask` 是死锁 ——
    它会在等待期间跑别的任务，那些任务可能又来 `submit`。世代号不匹配 → `wait` 空操作、
    `isComplete` 返回 `true`。
  - 验收：陈旧句柄上调 `wait` / `isComplete` 不崩、报完成；正常句柄 `wait` 之后
    `isComplete` 为真。

- [x] **2.4 `waitForAll` 收口**
  - 文件：同上
  - 做法：`m_scheduler->WaitforAll()` + 扫一遍池子回收。头文件注释里把 enkiTS 的限制
    （`TaskScheduler.h:356-357`）写清楚：这是屏障 / 关停用的，不是通用同步。
  - 验收：submit 一批不等，直接 `waitForAll()`，之后所有句柄都报完成、池子回到空闲。

### 阶段 3 · parallelFor、优先级、grain size

- [x] **3.1 `ParallelForOptions` 和阻塞版 `parallelFor`**
  - 文件：`task_system.{h,cpp}`
  - 做法：`min_range` 填给 `enki::TaskSet` 的 `m_MinRange`，`priority` 填 `m_Priority`。
    阻塞版仍然可以用栈上的 `TaskSet`（不进池）—— 生命周期由这个函数自己框住，
    进池反而是多余的开销。
  - 验收：`min_range = count` 时**只**产生一个 partition（在 lambda 里记录被调用的
    partition 数来断言）。

- [x] **3.2 异步版 `submitParallelFor`**
  - 文件：同上
  - 做法：和 `submit` 同一条路（进池、返回句柄），只是 `TaskSet` 带 `setSize` 和 `m_MinRange`。
  - 验收：拿到句柄、`wait` 之后结果正确；这是物理桥（层二）要的形状。

- [x] **3.3 `TaskPriority` 三档映射**
  - 文件：同上
  - 做法：`High/Normal/Low` → `TASK_PRIORITY_HIGH/MED/LOW`。头文件注释写明 enki 有五档、
    我们只暴露三档，以及 `priorityOfLowestToRun_` 这半边留着没做（D5）。
  - 验收：三档都能提交并完成（优先级的**效果**这一层不做断言 —— 那要靠
    `priorityOfLowestToRun_`，是 D5 明确留下的接缝）。

### 阶段 4 · pinned 任务与外部线程

- [x] **4.1 `submitPinned` 走池，删掉 `launchPinned` / `waitForPinnedTask` / `pinned`**
  - 文件：`task_system.{h,cpp}`
  - 做法：按 D8。`shared_ptr<enki::LambdaPinnedTask>` 进池、`AddPinnedTask`、返回句柄。
    旧的三个 API 删掉 —— 一次性用 `submitPinned` + `wait`，长驻用 `submitPinned` 不 wait。
  - 验收：连续两次 `submitPinned(1, ...)` 不再 use-after-free（现状第 7 条的回归用例）。

- [x] **4.2 `threadIndex` / `threadCount` / `workerCount` / `runPinnedTasks`**
  - 文件：同上
  - 做法：`threadIndex()` → `GetThreadNum()`，未注册线程返回 `kNoThread`（用 enki 的
    `NO_THREAD_NUM`）。`taskThreadCount()` 改名 `threadCount()`，补 `workerCount()`。
    `runPinnedTasks()` 直接转发。
  - 验收：`submitPinned(k, ...)` 的函数体里 `threadIndex() == k`。

- [x] **4.3 外部线程注册**
  - 文件：同上
  - 做法：`registerExternalThread()` / `deregisterExternalThread()` 转发 enkiTS 的同名调用
    （`TaskScheduler.h:401-409`）。配置里的 `external_thread_count` 填 `numExternalTaskThreads`。
  - 验收：起一个 `std::thread`，注册后它的 `threadIndex()` 不是 `kNoThread`、能 `submit`
    并 `wait`；注销后回到 `kNoThread`。
  - **这一步是渲染线程（层二）唯一的入口**，所以即使现在没人用也要做通并测到。

### 阶段 5 · 依赖图 `TaskGraph`

- [x] **5.1 `TaskGraph` 的建图 API**
  - 文件：`artichoco/core/task/task_graph.{h,cpp}`（新建）
  - 做法：按 D4。`add` / `addParallelFor` / `addPinned` / `addAfter({preds}, fn)`
    返回图内节点标识（**不是** `TaskHandle` —— 图还没提交，别让两种 ID 长得一样）。
    节点和它们的 `Dependency` 对象全部存在图里。
  - 验收：建图不提交时什么都不跑。

- [x] **5.2 提交整张图**
  - 文件：`task_graph.cpp` + `task_system.{h,cpp}`
  - 做法：加隐式终结节点（依赖所有出度为 0 的节点），整个存储体一个 `shared_ptr` 进池、
    返回一个 `TaskHandle`；然后只把入度为 0 的根节点 `AddTaskSetToPipe`。
    **依赖边必须在此之前全部连好**（D4）。
  - 验收：菱形图 `A → {B, C} → D`：断言 B / C 都在 A 之后、D 在 B 和 C 都完成之后、
    且 D 只执行一次。用原子计数 + 时间戳断言，不要用 sleep 凑。

- [x] **5.3 图里混 pinned 节点**
  - 文件：同上
  - 做法：确认 `IPinnedTask` 作为依赖方也能被 `OnDependenciesComplete` 正常启动
    （`TaskScheduler.h:202` 说明它有这条路）。
  - 验收：`decode(worker) → upload(pinned k)` 两节点的图能跑通，upload 体内
    `threadIndex() == k`。**这是层二里「解码 + 上传」那一行的形状**，先在测试里证明它成立。

### 阶段 6 · 测试、文档、收尾

- [x] **6.1 ArtiChoco 的第一个 ctest 目标**（**已提前到阶段 1 之后做**，见交接区的偏离 1、2）
  - 文件：`artichoco/core/tests/task_system_test.cpp`（新建）、`artichoco/core/CMakeLists.txt`
  - 做法：一个带 `main()` 的可执行 + `add_test`，不引测试框架，门用现成的 `BUILD_TESTING`
    （不是新开一个 `ARTICHOCO_BUILD_TESTS`）。照 `Tools/asset_tools/CMakeLists.txt:32` 抄形状。
  - 验收：`ctest` 里出现 `task_system_test` 并通过；`physics_smoke` /
    `asset_pipeline_smoke` 仍绿。

- [x] **6.2 「真的在多线程跑」那条断言**（**并且反向验证过它会失败**，见交接区）
  - 文件：同上
  - 做法：一个 `parallelFor(N, ...)`，每次迭代把 `threadIndex()` 记进一个
    `std::set`（或每线程 bucket 后合并）。`workerCount() > 0` 时断言**见过至少两个不同的下标**。
    N 和每次迭代的活量要足够大，让调度器真的有理由分片（配合 `min_range` 调）。
  - **这一条是本任务的核心验收。** 没有它，一个把所有活都在调用线程上跑完的实现也能过
    其它全部断言 —— 而那正是「没有真正意义上的多线程能力」。
  - 验收：断言通过；并且把 `worker_count = 1` 再跑一次，这条断言应当被跳过而不是失败
    （单 worker 下只有一个线程是正确行为）。

- [x] **6.3 其余测试用例**（TSan 没跑 —— clang-cl 在 Windows 上不支持 `-fsanitize=thread`）
  - 文件：同上
  - 做法：至少覆盖 —— 未 init 时 `get()` 抛；`parallelFor` 结果正确；`min_range = count`
    只一个 partition；池子上界；陈旧句柄安全；菱形依赖图顺序 + D 只跑一次；
    `submitPinned` 落在指定线程；两次 `submitPinned` 不崩（现状第 7 条回归）；
    外部线程注册 / 注销；`waitForAll` 之后池子空闲；带在途任务时 `shutdown()` 不崩。
  - 验收：全部通过。有条件的话再用 clang 的 TSan 跑一遍（`-fsanitize=thread`）——
    过不了也不阻塞，但结论要记进交接区。

- [x] **6.4 文档**
  - 文件：`ArtiChoco/README.md`（根 README 原先是空文件，补了一段任务系统入口 + 解码/上传
    两节点图）、`artichoco/core/task/README.md`（完整 API：生命周期、线程编号、grain size、
    句柄、`waitForAll` 的限制、三档优先级、`TaskGraph`、D10 四件不做的事）、
    `docs/Architecture/README.md`（第 7 节「资产管线多线程」改写成「多线程的消费者」，
    并新增 7.1 把层二那张表搬过来）
  - 做法：README 里写清 API、`waitForAll` 的限制、优先级只有三档、以及 D10 那四件不做的事。
  - 验收：一个没参与这次改动的人能只读文档就写出「解码 + 上传」那个两节点图。

- [x] **6.5 `examples/test_app` 跟上新 API** ← 取消：整个 `examples/test_app` 已删。
  example 另开项目写，不在本仓库跟。`ARTICHOCO_BUILD_EXAMPLES` 现在只还建 `asset_test`。

- [x] **6.6 三层 submodule 推指针**（ArtiChoco `bd6c171` → ArtiRenderer `cb01637`。还没 push origin）
  - 做法：ArtiChoco 提交 → ArtiRenderer 推 ArtiChoco 指针（`chore(deps)`）→
    ArtiEngine 推 ArtiRenderer 指针（`chore(deps)`）。
  - 验收：从干净克隆 `git submodule update --init --recursive` + `cmake --preset debug` +
    `cmake --build --preset debug` + `ctest` 全通。

---

## 端到端验收

全部做完之后，下面每条都要真的跑一遍：

1. `cmake --preset debug` + `cmake --build --preset debug` 干净通过（无新增 warning）。
2. `ctest` 三个目标全绿：`task_system_test`、`physics_smoke`、`asset_pipeline_smoke`。
3. **「真的多线程」那条断言过**（6.2）—— 一个 `parallelFor` 被至少两个不同线程下标执行过。
4. **池子有上界**：十万次 submit + 等待之后 `slotCount()` 不随次数增长。
5. **陈旧句柄安全**：回收过的句柄上 `wait` / `isComplete` 不崩、报完成。
6. **菱形依赖图**顺序正确且汇点只跑一次。
7. **pinned 落在指定线程**，且 `decode(worker) → upload(pinned)` 的两节点图能跑通。
8. **外部线程**注册后能 submit / wait，注销后 `threadIndex()` 回到 `kNoThread`。
9. `scene_editor` 起来、开项目、Play / Simulate 都正常（证明 D2 挪生命周期没破坏什么）。
10. `arti_player projects/<项目>.artiproj` 正常跑。
11. `asset_tools scan <项目>` 正常跑 —— **这条专门证明现状第 2 条修好了**：
    CLI 进程里现在有一个活的 `TaskSystem`。
12. ~~`cmake -DARTICHOCO_BUILD_EXAMPLES=ON` 配置 + 编译通过（6.5）~~ —— 取消。`examples/test_app` 已删。
13. 调试器线程窗口里 worker 有名字、数量对得上 `workerCount()`。
14. 干净克隆走一遍 6.6 的流程。

**不在验收范围内**：任何真实消费者的性能提升。这次没有消费者（D1），所以**不要拿帧时间或导入
耗时当验收指标** —— 那是层二的事，现在测出来的任何数字都只是噪声。

---

## 风险与注意

### 三层 submodule

改动几乎全在 `ArtiRenderer/ArtiChoco`，也就是**嵌套两层**的 submodule。提交顺序是固定的：
ArtiChoco → ArtiRenderer 推指针 → ArtiEngine 推指针。漏掉中间一层的话，别人克隆下来编不过，
而错误信息会指向「找不到某个头」，跟真实原因隔着两层。

### ArtiChoco 工作区已经有别人的改动

开工前先把 `nvrhi_vulkan_dispatch.cpp` 那个 `vkGetDeviceProcAddr` 改动**单独提交掉**
（见交接区第一条）。不然它会被卷进 job system 的 commit 里。

### `examples/test_app` 已删

那个 example 过时了（NVRHI 迁移后就编不过），example 另开项目写。`ARTICHOCO_BUILD_EXAMPLES`
现在只还建 `asset_test`。下面「背景与现状」里提到它的地方是开工时的记录，没改。

### 别持锁调 `WaitforTask`

`WaitforTask` 在等待期间会**跑别的任务**（`TaskScheduler.h:347-354`），那些任务可能又来
`submit`，于是要拿同一把锁 —— 死锁。2.3 那条「先拷 `shared_ptr` 再解锁再等」不是风格问题。
同理 `parallelFor` 的函数体里不要去 `wait` 一个非自己子任务的句柄（enkiTS 的注释也这么警告：
`Only wait for child tasks of the current task otherwise a deadlock could occur`）。

### 依赖边必须在入队之前连好

D4 已经说了原因。写测试时特别注意：**先 `submit` 再连边**会得到一个「永远不启动的依赖方」，
而它的表现是「测试挂在 `wait` 上」而不是「报错」。菱形图那条用例要加超时保护。

### 别跑 clang-format

仓库根有 `.clang-format`，但既有文件没按它格式化过。改老文件（`task_system.*`、
`application.*`、`entry_point.cpp`）时手写成周围的风格。
注意 `task_system.h` 现在的缩进和 `log.h` 不完全一致，跟着**各自文件**的风格走。

### `Application` 的公开头会变

删 `m_task_system` 动的是 `application.h`，那是几乎所有下游都包含的头 —— 会触发全量重编。
安排在一次干净的构建上做，别和别的改动混在一起查问题。

---

## 参考

- `ArtiRenderer/ArtiChoco/third_party/enkiTS/src/TaskScheduler.h` —— 唯一权威。
  重点行号：优先级 `:88-103`、`ICompletable` / `SetDependency` `:109-142`、
  `ITaskSet` 与 `m_MinRange` `:146-186`、`IPinnedTask` `:188-203`、`Dependency` `:231-249`、
  `ProfilerCallbacks` `:251-263`、`TaskSchedulerConfig` `:278-294`、
  `WaitforTask` `:354`、`WaitforAll` 的限制 `:356-357`、
  `GetNumTaskThreads` / `GetThreadNum` `:384-392`、外部线程 `:401-409`
- `ArtiRenderer/ArtiChoco/third_party/enkiTS/example/Dependencies.cpp` —— 建图的正确写法
- `ArtiRenderer/ArtiChoco/third_party/enkiTS/example/ExternalTaskThread.cpp` —— 外部线程注册
- `ArtiRenderer/ArtiChoco/artichoco/core/log.h:77-80` —— 进程级 init / shutdown 的先例
- `docs/Architecture/README.md` 第 7 节 —— 「资产管线多线程」那条空缺的原文
- `docs/Architecture/Rendering.md` 第 1 节 —— 「工作线程解码、渲染线程上传」的既有界限

