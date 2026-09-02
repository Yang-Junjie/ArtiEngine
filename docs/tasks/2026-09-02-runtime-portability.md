# 可搬移的运行时：DLL staging + shader / 字体两段查找 + pack 收尾

| | |
| --- | --- |
| **状态** | 已完成（待 push）|
| **创建** | 2026-09-02 |
| **最后更新** | 2026-09-02 |
| **涉及仓库** | ArtiEngine（本仓库）、ArtiRenderer（submodule）、ArtiChoco（ArtiRenderer 的 submodule） |
| **目标** | 让 `build/bin` 里的产物在**没有源码树、SDK 不在 PATH** 的机器上能跑；让 `asset_tools pack` 的产物真的「双击就运行」 |

---

## 交接区

> **全文唯一允许改动的段落。每次收工前更新这里。**

**当前进度**：**全部完成**，阶段 0～7 共 36 步全部打勾。最后一轮（阶段 6 Release 验收 +
阶段 7 产物自带 CRT）的结论：Release 产物在 clean PATH、两条回落路径失效的情况下渲染正常，
且进程加载的 CRT / SDL3 / slang 都来自产物目录而不是 System32。

**下一步**：只剩 **push**（三层，由内向外，见 5.3）。推送是对外动作，等用户点头。

**没验的那一条**（唯一遗留）：没在一台真正没装 VC++ Redistributable / 没装 Vulkan 驱动的机器
上跑过产物。本机卸不掉 redist，所以拿到的最强证据是「进程加载的是产物里那一份」——
exe 所在目录的搜索优先级高于 System32，而这几个 DLL 都不是 KnownDLLs。要真验就得找一台干净
的虚拟机。

**做完之后的事实**（下一个人可以直接依赖这些）：

- `core::executableDir()` 在 `ArtiChoco/artichoco/core/io/paths.h`，Windows / Linux 有实现。
- 着色器：`exe/shaders/` 优先，回落 `ARTIRENDERER_SHADER_DIR`。**整目录二选一**，
  选中哪个根有一条 info 日志。加新 `.slang` 不用改 staging（整目录拷）。
- 字体：`exe/resources/` 优先，回落 `ARTIENGINE_TOOLS_RES_DIR`。
- 三个 exe + `asset_pipeline_smoke` 都 staging 运行时依赖；`arti_player` / `scene_editor` /
  `asset_tools` 还 staging shader。
- `pack` 会把 `*.dll` + `shaders/` + `arti_player` 写进产物，来源是 `asset_tools` 自己的 exe
  目录。新开关 `--no-runtime` / `--no-player`。
- Debug 下 `build/bin` 应该恰好有 3 个 DLL：`SDL3d.dll` / `slang.dll` / `slang-compiler.dll`。
  **多出 `slang-glslang*.dll` 之类说明有人把不需要的卫星 DLL 也拷了**，见 D6。

**遗留 / 后续**（不属于本任务，已记进 `docs/Architecture/README.md` 的「明确未做」）：

- **Release 发布未验收**。可搬移性是在 Debug 下验的，而 Debug 链的是调试 CRT
  （`ucrtbased.dll` / `MSVCP140D.dll` / `VCRUNTIME140D.dll`）—— 那几个不可再分发，本机能跑只
  是因为 System32 里装了。真要给别人跑必须 Release 构建打包，那条路还没走过。
- **非 Windows 的可搬移性未验**。`artichoco_stage_vulkan_sdk_runtime()` 在非 Windows 上只设
  `BUILD_RPATH`、不拷文件；`copyRuntimeFiles()` 里「没有 DLL 算失败」那条也包了 `_WIN32`。
- **着色器仍是运行期编译**。产物里放的是 `.slang` 源码，不是 `.spv`。离线预编译是另一件事。
- **打包没有编辑器入口**，只有 CLI。

**阶段 0 的基线记录**（2026-09-02，动手前的状态，留作对照）：

```
build/bin/ 里的 DLL：
  SDL3d.dll    6145464  08-28_13:12   ← 残留物，和已删掉的 cube_scene.exe(08-28_13:22) 同期
  slangd.dll     82360  08-28_13:12   ← 同上，而且 exe 根本不加载它（见 F1）
grep -c copy_if_different build/build.ninja  →  0
cmake --version  →  4.2.1
PATH 上有 C:\VulkanSDK\1.4.335.0\Bin        ← 正是它掩盖了问题
```

**阶段 0 的实测结论**（每条都影响了后续步骤，留着是因为它们解释了代码里那几段注释为什么存在）：

- **F1 · `ArtiVulkanSDK.cmake` 的 slang Debug 配对是错的。**
  链接行用的是 `slangd.lib`，但 `llvm-readobj --coff-imports slangd.lib` 显示它内嵌的 DLL 名是
  **`slang.dll`**；而 `IMPORTED_LOCATION_DEBUG` 指向 `slangd.dll` —— 那是 Slang 的 language
  server（`slangd.exe` 的伴生 DLL），不是 debug 版 slang。所以 staging 会拷一个 exe 从不加载
  的文件，而真正需要的 `slang.dll` 一个都不拷。→ 步骤 1.5 修的就是这个。
- **F2 · `slang.dll` 只有 46 KB，是个转发器。** 真正 23 MB 的实现在 `slang-compiler.dll` 里，
  由 `slang.dll` 在运行时 `LoadLibrary` 加载 —— **CMake 看不见这条依赖**。→ 步骤 1.6 / D6。
- **F3 · 实测最小 DLL 集（Debug）就三个**：`SDL3d.dll` + `slang.dll` + `slang-compiler.dll`。
  **`slang-glslang*.dll` / `slang-rt*.dll` / `slang-glsl-module*.dll` 都不需要** ——
  SPIR-V 直出不过 glslang。别多拷（那三个加起来 30 MB+）。
- **F4 · 复现方式**：原 PATH 只摘掉 `C:\VulkanSDK\<ver>\Bin`，改动前的 `arti_player.exe`
  立刻 `0xC0000135`（STATUS_DLL_NOT_FOUND）。不需要清空整个 PATH。
- **F5 · Debug 构建不可发布**（见上面「遗留」）。
- **F6 · 本机 CMake 4.2.1**，`copy_directory_if_different` 可用；2.3 里仍然保留了 3.25 的降级
  分支，因为仓库声明的下限是 3.25。

**踩到的坑**（方法论，给后面的人省时间）：

- PowerShell 里 `& exe ... | Select-Object -First N` 会**提前终止管线**，`$LASTEXITCODE` 是
  假的（我一开始拿到 `0xC0000135`，其实进程正常退出了）。测退出码要
  `& exe > out.txt 2> err.txt` 再读 `$LASTEXITCODE`。
- 判断「缺哪个 DLL」不要靠猜。`llvm-readobj --coff-imports <pe>` 列静态导入表
  （`llvm-readobj` 在 `C:\Program Files\LLVM\bin`）；**动态 `LoadLibrary` 的依赖它看不见**，
  那种只能靠「往空目录里逐个加文件」二分出来（F2 就是这么找到的）。
- **spdlog 不会立刻落盘。** `Stop-Process -Force` 杀掉进程会丢掉缓冲里的日志，看起来像是
  「跑到一半卡住了」。要看完整日志得让它干净退出：PowerShell 里 `$p.CloseMainWindow()` +
  `$p.WaitForExit()`，日志末尾才会出现 `First frame rendered` 和 `ArtiChoco stopped`。
- **整个源码树改不了名**：工具的 shell 把 cwd 钉在里面，Windows 上会锁住。要让「回落路径失效」
  就只改名那两个叶子目录（`ArtiRenderer/ArtiRenderer/src/shaders` 和 `Tools/resources`），
  证明力等价 —— 它们就是两个宏指向的地方。

**决定记录**（时间倒序，新的加在最上面）：

- 2026-09-02 1.4 没写临时代码，改用 2.1 的 shader 根日志验证 `executableDir()` —— 那是产品
  代码里的真实调用点，比临时代码强，也不用事后删。
- 2026-09-02 2.3 用 `copy_directory_if_different` 但包了一层 CMake 版本降级分支，而不是只在
  文档里留一句提醒。
- 2026-09-02 `PackReport` 多加了 `warnings` —— 「缺播放器只提示不失败」需要一个不影响
  `succeeded` 的出口，而这个库不该自己决定怎么打日志。
- 2026-09-02 因 F1 / F2 新增 **D6**（显式 staging slang 卫星 DLL）和步骤 1.5 / 1.6。
- 2026-09-02 初始决定见「设计决定」D1～D5。
---

## 背景与现状

上一个 commit（`6067d51 feat(runtime): 独立播放器与打包`）交付了独立播放器和 `asset_tools pack`，
但产物实际上不可搬移。三条已验证的证据：

### 证据 1：没有任何 target 在 staging 运行时 DLL

`artichoco_stage_vulkan_sdk_runtime()`（定义在 `ArtiRenderer/ArtiChoco/cmake/ArtiVulkanSDK.cmake:221`）
只被这几个地方调用：

```
ArtiRenderer/ArtiChoco/examples/test_app/CMakeLists.txt:142
ArtiRenderer/samples/basic_window/CMakeLists.txt:15
ArtiRenderer/tests/CMakeLists.txt:5
```

而根 `CMakeLists.txt` 把 `ARTIRENDERER_BUILD_SAMPLES` / `ARTIRENDERER_BUILD_TESTS` 强制 OFF，
`ARTICHOCO_BUILD_EXAMPLES` 默认 OFF。所以这三处一个都不参与构建：

```
$ grep -c "copy_if_different" build/build.ninja
0
```

`build/bin/SDL3d.dll` 和 `slangd.dll` 的时间戳是 `08-28 13:12`，和同目录那个**已经从 CMake 里
删掉**的 `cube_scene.exe`（`08-28 13:22`）同期 —— 它们是残留物。现在 exe 能起来是靠这两个残留
文件，或者 Vulkan SDK 的 bin 在 PATH 上。`rm -rf build` 重建之后会缺 DLL。

### 证据 2：shader 路径是源码树的绝对路径，且没有 fallback

`ArtiRenderer/ArtiRenderer/src/detail/shader_paths.cpp` 全文只有一句：

```cpp
return std::filesystem::path{ ARTIRENDERER_SHADER_DIR } / name;
```

宏由 `ArtiRenderer/ArtiRenderer/CMakeLists.txt:41` 注入，值是
`${CMAKE_CURRENT_SOURCE_DIR}/src/shaders`。着色器是**运行期**由 Slang 编译的
（`detail::shaderPath()` 在 12 处 pass 代码里被调用，都是建 PSO 的时候），所以 `.slang` 文件必须在运行时存在。

### 证据 3：字体同理

`Tools/scene_editor/CMakeLists.txt:25` 注入 `ARTIENGINE_TOOLS_RES_DIR`，
`Tools/scene_editor/src/editor_layer.cpp:84` 用它拼字体路径。缺字体不致命（`ImGuiHost` 会退回
内建位图字体并记一条 warn），但同一个问题，一起修。

全树只有这两处 `_DIR="` 注入，所以范围就是这么大：

```
$ grep -rn '_DIR="' --include=CMakeLists.txt . | grep -v '^./build'
./ArtiRenderer/ArtiRenderer/CMakeLists.txt:41:    ARTIRENDERER_SHADER_DIR=...
./Tools/scene_editor/CMakeLists.txt:25:    ARTIENGINE_TOOLS_RES_DIR=...
```

### 结论

`Tools/asset_tools/asset_packer.h` 里那句「把 `arti_player.exe` 拷进 `<out>/` 就能双击运行」
目前只在开发机上、且源码树没挪窝、且 SDK 在 PATH 上时成立。

---

## 设计决定

### D1 · exe 目录怎么拿 —— 已定：在 `ArtiChoco::Core` 加 `core::executableDir()`

用平台 API（Windows `GetModuleFileNameW`，Linux 读 `/proc/self/exe`），结果缓存在
function-local static 里。

**不用 `argv[0]`**：通过 PATH 启动时它可能只是一个文件名，推不出目录。
**不用 `std::filesystem::current_path()`**：双击 exe 时 cwd 不是 exe 所在目录，从资源管理器
拖文件进去时更不是。

放在 `ArtiChoco::Core` 而不是各层自己写一份：`artirenderer` 和 `arti_tools_asset` 都要用，
而 Core 已经在它们的依赖链上（PUBLIC），不引入新依赖。

代价：**要改 ArtiChoco submodule**，也就是三层提交（见「风险与注意」）。

### D2 · shader 查找：整目录二选一，**不做逐文件回落** —— 已定

```
exe 目录/shaders/ 存在且里面至少有一个 .slang  →  用它
否则                                          →  用 ARTIRENDERER_SHADER_DIR
```

判定只做一次，缓存起来；选中哪个根**记一条 info 日志**（排查时这一行能省很多时间）。

**为什么不逐文件回落**（这条最容易被「顺手简化」掉，所以写清楚）：`.slang` 之间有
`#include`（`ibl_common.slang` 被 `irradiance` / `prefilter` / `deferred_lighting` 引用），
而 Slang 解析 include 是**相对包含它的那个文件**。逐文件回落一旦出现「A 在 exe 旁边、
它 include 的 B 只在源码树」，就会变成一个跟路径无关的编译错误 —— 比「文件找不到」难查得多。
整目录二选一保证 include 永远在同一个根里解析。

### D3 · shader 拷贝：加一个 `artirenderer_stage_shaders(target)` 函数 —— 已定

和现有的 `artichoco_stage_vulkan_sdk_runtime(target)` 同一个形状：由**消费方 exe** 调用，
POST_BUILD 把整个 shader 目录拷到 `$<TARGET_FILE_DIR:target>/shaders`。

不做成「一个 custom target + 所有 exe 依赖它」：那样就把路径耦合到全局
`CMAKE_RUNTIME_OUTPUT_DIRECTORY` 上了。函数形式让每个 exe 自己决定要不要 ——
`asset_tools` 自己不渲染，但它仍然要调（阶段 4 的 pack 从自己旁边取 shader，见 D5）。

### D4 · 字体：同样两段查找，但只有 `scene_editor` 需要 —— 已定

`exe 目录/resources/` 优先，回落 `ARTIENGINE_TOOLS_RES_DIR`。POST_BUILD 直接写在
`Tools/scene_editor/CMakeLists.txt` 里，不额外做函数 —— 只有一个消费方。

### D5 · pack 从「自己 exe 旁边」取运行时文件 —— 已定

`pack` 拿不到 `ARTIRENDERER_SHADER_DIR`（那是 `artirenderer` 的 PRIVATE compile definition），
也不该去猜源码树在哪。而阶段 2 做完之后，`asset_tools.exe` 旁边就已经有 `shaders/` 和
所有 DLL 了（三个 exe 共享 `build/bin`，`TARGET_RUNTIME_DLLS` 的并集都落在那）。

所以 `PackOptions` 加一个 `runtime_dir`（默认由调用方填 `core::executableDir()`，空 = 跳过），
pack 从它拷 `*.dll` + `shaders/**`，另外可选拷 `arti_player.exe`。

**缺 `arti_player.exe` 时记 warn 而不是失败**：CI 里可能把 pack 和 player 的构建分开跑。
缺 DLL 或 `shaders/` 则算失败 —— 打出一份跑不起来的包，比打包失败糟得多（和现有
`checkIntegrity()` 前置校验同一个态度）。

### D6 · slang 的卫星 DLL 显式 staging —— 已定（阶段 0 实测后新增）

`slang.dll`（46 KB）只是转发器，实现在 `slang-compiler.dll`（23 MB）里，由它在运行时
`LoadLibrary` 加载。CMake 的 `$<TARGET_RUNTIME_DLLS>` 只跟 target 依赖图，**看不见动态加载**，
所以必须在 `artichoco_stage_vulkan_sdk_runtime()` 里显式补一条。

只补 `slang-compiler.dll`。`slang-glslang*.dll` / `slang-rt*.dll` / `slang-glsl-module*.dll`
经实测都不需要（SPIR-V 直出不过 glslang），三个加起来 30 MB+，不要顺手都拷进去。

配套要修 F1：`IMPORTED_LOCATION_DEBUG` 现在指向 `slangd.dll`（Slang 的 language server），
而链接行用的 `slangd.lib` 内嵌的 DLL 名是 `slang.dll`。两配置统一用 `slang.lib` + `slang.dll`
—— Slang 上游只发一份 DLL，本来就没有 debug / release 之分。

### 待定：无

D1～D5 全部已定。执行时如果发现某条行不通，**先在交接区记下来再改**，不要默默换方案。

---

## 任务清单

### 阶段 0 · 复现现状（先做，别跳）

- [x] **0.1 记录基线：当前 build/bin 里有什么**
  - 命令：`ls -la --time-style=+%m-%d_%H:%M build/bin/`
  - 做法：把 `*.dll` 的文件名和时间戳抄进交接区。
  - 验收：交接区里有这份清单。
  - 结果：`SDL3d.dll` / `slangd.dll` 均为 `08-28_13:12` 的残留物。

- [x] **0.2 确认没有 staging 规则**
  - 命令：`grep -c "copy_if_different" build/build.ninja`
  - 验收：输出是 `0`。**如果不是 0**，说明有人已经动过了 —— 停下，先在交接区记清楚。
  - 结果：`0`，符合预期。

- [x] **0.3 复现「缺 DLL 起不来」**
  - 做法：原 PATH 只摘掉 `C:\VulkanSDK\<ver>\Bin`，跑 `build/bin/arti_player.exe --help`。
  - 验收：进程以 `0xC0000135`（STATUS_DLL_NOT_FOUND）失败。
  - 结果：复现成功，见交接区 F4。顺带靠「空目录里逐个加 DLL」二分出了 F1 / F2 / F3 ——
    最小 DLL 集是 `SDL3d.dll` + `slang.dll` + `slang-compiler.dll`。

### 阶段 1 · `core::executableDir()` + 运行时依赖修正（改 ArtiChoco）

- [x] **1.1 新建 `artichoco/core/io/paths.h`**
  - 文件：`ArtiRenderer/ArtiChoco/artichoco/core/io/paths.h`（新建）
  - 做法：`namespace arti::core` 里声明
    `const std::filesystem::path& executableDir();`。注释写清楚「为什么不用 argv[0] /
    current_path」（照抄 D1 的理由）。
  - 验收：文件存在，风格和邻居（`io/input.h`）一致。
  - 结果：新建完成，按 ArtiChoco core 的风格（次行大括号）。

- [x] **1.2 实现 `artichoco/core/io/paths.cpp`**
  - 文件：`ArtiRenderer/ArtiChoco/artichoco/core/io/paths.cpp`（新建）
  - 做法：Windows 走 `GetModuleFileNameW`（缓冲不够时倍增重试），Linux 走
    `std::filesystem::read_symlink("/proc/self/exe")`，其他平台 `#error` 并在注释里指出
    macOS 补 `_NSGetExecutablePath` 即可。结果存在 function-local static，取 `parent_path()`。
    失败时抛 `std::runtime_error` —— 拿不到 exe 目录之后一切路径都是错的，不该静默兜底。
  - 验收：编译通过。
  - 注意：Windows 下要 `#define NOMINMAX` 之前先看 `renderer/CMakeLists.txt` 的做法，
    这里如果只用 `GetModuleFileNameW` 应该不需要。
  - 结果：Windows 走 GetModuleFileNameW（倍增重试），Linux 读 /proc/self/exe，其他平台 #error。

- [x] **1.3 挂进 core 的源文件列表**
  - 文件：`ArtiRenderer/ArtiChoco/artichoco/core/CMakeLists.txt`
  - 做法：在 `add_library(artichoco_core STATIC ...)` 里加 `io/paths.cpp`（按现有的字母序，
    排在 `io/input.cpp` 后面）。
  - 验收：`cmake --preset debug && cmake --build --preset debug` 通过。
  - 结果：已加在 io/input.cpp 后面；`grep -c io/paths.cpp build/build.ninja` = 2，编译通过。

- [x] **1.4 冒烟验证**
  - 做法：临时在 `arti_player` 的 `--help` 分支或 `PlayerLayer::onAttach` 里打一行
    `executableDir()`，跑一次确认它是 `build/bin`。**验证完把临时代码删掉。**
  - 验收：输出等于 `build/bin` 的绝对路径。
  - 结果：没写临时代码 —— 改用 2.1 的 shader 根日志验证，它打印的就是 executableDir() 推出来的路径：
    `Loading shaders from 'H:\...uildin\shaders' (staged next to the executable)`。
    比临时代码强：它是产品代码里的真实调用点，不用事后删。
- [x] **1.5 修 slang 的 Debug 配对（F1）**
  - 文件：`ArtiRenderer/ArtiChoco/cmake/ArtiVulkanSDK.cmake`（第 156～190 行那段）
  - 做法：Slang 上游只发一份 DLL，没有 debug / release 之分。所以两个配置统一：
    - `find_library(_arti_slang_library_debug NAMES slangd slang ...)` → 去掉 `slangd`，
      只找 `slang`（`slangd.lib` 是 language server 的导入库，内嵌 DLL 名却是 `slang.dll`，
      留着它只会让链接行和 staging 对不上）
    - `find_file(_arti_slang_runtime_debug NAMES slangd.dll slang.dll ...)` → 只找 `slang.dll`
    - 顺手在上面加一段注释说明为什么（照抄 D6 的理由），否则下一个人会觉得「debug 就该用
      slangd」又改回去
  - 验收：`cmake --preset debug` 之后，`grep -i slang build/build.ninja | grep -o '[^ ]*\.lib'`
    里出现的是 `slang.lib` 而不是 `slangd.lib`；重新构建后
    `llvm-readobj --coff-imports build/bin/arti_player.exe | grep slang` 仍是 `slang.dll`。
  - 结果：`grep -o "slang[a-z-]*\.lib" build/build.ninja` 只剩 `slang.lib`；
    `llvm-readobj --coff-imports build/bin/arti_player.exe` 里是 `slang.dll`，与 staging 对上了。

- [x] **1.6 `artichoco_stage_vulkan_sdk_runtime()` 补 slang 卫星 DLL（F2 / D6）**
  - 文件：`ArtiRenderer/ArtiChoco/cmake/ArtiVulkanSDK.cmake`（第 221 行那个 function）
  - 做法：在现有的 `$<TARGET_RUNTIME_DLLS>` 拷贝之后，再加一条 POST_BUILD 拷
    `slang-compiler.dll`。路径用 `find_file` 找一次存进变量（和 `_arti_slang_runtime_release`
    同一个套路，`PATH_SUFFIXES Bin bin`），**不要**硬编码 SDK 布局。
    注释里写清楚「CMake 看不见 `LoadLibrary`，所以这条必须手写」，以及
    「glslang / slang-rt / slang-glsl-module 实测不需要，别加」。
  - 验收：阶段 2.4 之后 `build/bin/slang-compiler.dll` 存在，且
    `ls build/bin/*.dll` 里**没有** `slang-glslang*.dll` / `slang-rt*.dll`。
  - 结果：`build/bin/slang-compiler.dll` 已在位；build/bin 下只有 3 个 DLL
    （SDL3d / slang / slang-compiler），没有 glslang / slang-rt。同时给 asset_pipeline_smoke 也加了
    staging（ctest 不该依赖 SDK 在 PATH 上）。


### 阶段 2 · shader 两段查找 + staging（改 ArtiRenderer + 本仓库）

- [x] **2.1 改 `shaderPath()` 成整目录二选一**
  - 文件：`ArtiRenderer/ArtiRenderer/src/detail/shader_paths.cpp`
  - 做法：加一个内部函数解析根目录（照 D2：`executableDir()/shaders` 里能找到至少一个
    `.slang` 就用它，否则用 `ARTIRENDERER_SHADER_DIR`），结果缓存进 function-local static，
    并记一条 info：选中了哪个根。日志用 `arti::rendering::getLogChannel()`
    （声明在 `src/detail/log.h`，频道名 "ArtiRenderer"）—— 注意 `arti::renderer::getLogChannel()`
    是另一个同名函数（ArtiChoco 的 RHI 层，频道名 "ArtiRHI"），别拿错。
    `shaderPath(name)` 变成 `root() / name`。
  - 验收：编译通过；跑 `scene_editor`，日志里能看到选中的根是源码树（因为还没 staging）。
  - 结果：整目录二选一 + 一次性缓存 + info 日志已实现；判定条件是「目录存在且里面至少有一个
    .slang」（空壳目录不算）。

- [x] **2.2 更新 `shader_paths.h` 的注释**
  - 文件：`ArtiRenderer/ArtiRenderer/src/detail/shader_paths.h`
  - 做法：现在那段注释说「二进制不可搬移 / 真要发布时改这一个函数即可」，已经过期了 ——
    改成描述两段查找和它的顺序。顺手修掉声明行开头那个多余的空格。
  - 验收：注释和实现一致。
  - 结果：注释改成描述两段查找及其顺序；声明行开头多余的空格一并修了。

- [x] **2.3 加 `artirenderer_stage_shaders(target)` 函数**
  - 文件：`ArtiRenderer/ArtiRenderer/CMakeLists.txt`
  - 做法：先把 shader 源目录提成一个 **CACHE INTERNAL** 变量，第 41 行注入宏和下面的函数
    都用它，别写两遍字面量：
    ```cmake
    set(ARTIRENDERER_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
        CACHE INTERNAL "ArtiRenderer builtin shader sources")
    ```
    然后定义 function，POST_BUILD 用
    `cmake -E copy_directory_if_different ${ARTIRENDERER_SHADER_SOURCE_DIR}
    $<TARGET_FILE_DIR:target>/shaders`。开头照 `artichoco_stage_vulkan_sdk_runtime` 对未知
    target 报 `FATAL_ERROR`。
  - 验收：`cmake --preset debug` 配置通过（此时还没人调它）。
  - **坑 1 · 变量作用域**：函数体里的 `${CMAKE_CURRENT_SOURCE_DIR}` 在**调用点**展开，不是
    定义点 —— 所以绝不能在函数里用它拼 shader 路径，会指到 `Runtime/player` 之类的地方去。
    同理，普通 `set()` 出来的变量在兄弟目录里看不见，必须 `CACHE INTERNAL`。
  - **坑 2 · 函数可见性**：CMake 函数一旦定义就是全局的，但必须**先定义后调用**。根
    `CMakeLists.txt` 的顺序是 ArtiRenderer → third_party → ArtiEngine → Runtime → Tools，
    所以定义在 `ArtiRenderer/ArtiRenderer/CMakeLists.txt` 里、被 `Runtime` / `Tools` 调用是
    成立的。别把定义挪到更后面。
  - **坑 3 · CMake 版本**：`copy_directory_if_different` 需要 CMake ≥ 3.26，而本仓库
    `cmake_minimum_required` 是 3.25。如果目标环境是 3.25，退回 `copy_directory`
    （每次都拷，13 个小文件，代价可忽略）。**实际用了哪个记进交接区。**
  - 结果：用了 `copy_directory_if_different`，但包了一层 `CMAKE_VERSION VERSION_GREATER_EQUAL 3.26`
    的降级分支（低版本回落 `copy_directory`）—— 比只在文档里留一句提醒靠得住。
    本机 CMake 4.2.1，走的是 `copy_directory_if_different`。

- [x] **2.4 三个 exe 调 staging**
  - 文件：`Runtime/player/CMakeLists.txt`、`Tools/scene_editor/CMakeLists.txt`、
    `Tools/asset_tools/CMakeLists.txt`
  - 做法：**三个 exe 都调这两个函数**，各加在自己 CMakeLists 末尾：
    ```cmake
    artichoco_stage_vulkan_sdk_runtime(<target>)
    artirenderer_stage_shaders(<target>)
    ```
    `asset_tools` 自己不渲染、不需要 shader，但仍然要调 —— 阶段 4 的 pack 是从**自己 exe
    旁边**取运行时文件的（见 D5）。三个 exe 事实上共享 `build/bin`，但依赖「别人顺手拷过来
    的东西」是隐式耦合，显式调更稳。
  - 验收：重新构建后 `build/bin/` 下同时有 `SDL3*.dll`、`slang*.dll` 和 `shaders/*.slang`
    （13 个）；`grep -c copy_if_different build/build.ninja` 不再是 0。
  - 结果：三个 exe 均已调两个函数。`build/bin/` 现在有 3 个 DLL + `shaders/`（13 个）
    + `resources/`；`grep -c "copy_if_different\|copy_directory" build/build.ninja` = 4（原本 0）。
    附带清掉了 `build/bin/slangd.dll` 这个 08-28 的残留物。

- [x] **2.5 验证真的在用 exe 旁边那份**
  - 做法：`rm -rf build && cmake --preset debug && cmake --build --preset debug`，
    跑 `scene_editor`，看日志里选中的根是 `build/bin/shaders`。
  - 验收：日志确认；界面正常出图。
    日志里的 shader 根是 `build/bin\shaders`，`gbuffer.slang` 从 staged 那份编成了 SPIR-V，
    无 error。**差一个 `rm -rf build` 的全新构建** —— 并到阶段 5.1 一起做，避免重建两次。
  - 结果：全新构建（`rm -rf build` 后 216 个目标）已做。`build/bin` 里恰好是 3 个 DLL
    （SDL3d / slang / slang-compiler）+ `shaders/`（13）+ `resources/`，没有任何残留物。clean PATH 下
    arti_player 和 scene_editor 都起来，日志里 shader 根是 `build/bin\shaders`。

- [x] **2.6 更新 CMake 里的过期注释**
  - 文件：`ArtiRenderer/ArtiRenderer/CMakeLists.txt`（第 39～41 行那段
    「代价是二进制不可搬移」）
  - 做法：改成「宏是回落路径，优先用 exe 旁边的 `shaders/`」。
  - 验收：注释和实现一致。
  - 结果：改成「这个宏是回落路径，优先用 exe 旁边 staging 过的那份」。

### 阶段 3 · 字体两段查找（本仓库）

- [x] **3.1 editor 字体路径改两段查找**
  - 文件：`Tools/scene_editor/src/editor_layer.cpp`（第 84 行附近）
  - 做法：先看 `core::executableDir()/resources/fonts/...` 在不在，不在再回落
    `ARTIENGINE_TOOLS_RES_DIR`。逻辑抽成一个文件内静态小函数，别把三元表达式塞进
    `imgui_info.font_path` 那一行。
  - 验收：编译通过，编辑器字体正常（中文能显示）。
  - 结果：抽成匹名空间里的 `uiFontPath()`；clean PATH 下跑编辑器，日志是
    `Loaded the UI font 'NotoSansSC-Regular.ttf' at 18px (with common simplified Chinese)`。

- [x] **3.2 scene_editor POST_BUILD 拷 resources**
  - 文件：`Tools/scene_editor/CMakeLists.txt`
  - 做法：POST_BUILD 把 `Tools/resources` 拷到 `$<TARGET_FILE_DIR:scene_editor>/resources`；
    同时更新第 22～23 行那段「代价是二进制不可搬移」的注释。
  - 验收：`build/bin/resources/fonts/Noto_Sans_SC/` 存在；把源码树临时改名后编辑器字体
    仍然正常（或至少路径解析到了 exe 旁边）。
  - 结果：POST_BUILD 已加，`build/bin/resources/fonts/Noto_Sans_SC/static/NotoSansSC-Regular.ttf`
    在位；第 22～23 行那段「不可搬移」注释已改。

### 阶段 4 · pack 写进运行时文件（本仓库）

- [x] **4.1 `PackOptions` / `PackReport` 加字段**
  - 文件：`Tools/asset_tools/asset_packer.h`
  - 做法：`PackOptions` 加
    `std::filesystem::path runtime_dir;`（注释说明：默认由调用方填 `core::executableDir()`，
    空 = 只打资产、跳过运行时文件）和 `bool copy_player{ true };`。
    `PackReport` 加 `std::size_t runtime_files_copied{ 0 };`。
  - 验收：编译通过。
  - 结果：`runtime_dir` / `copy_player` 加在 `PackOptions`，`runtime_files_copied` 和
    **`warnings`** 加在 `PackReport` —— warnings 是多出来的：「缺播放器只提醒不失败」需要一个
    不影响 `succeeded` 的出口，而这个库不应该自己决定怎么打日志。

- [x] **4.2 pack 实现拷贝**
  - 文件：`Tools/asset_tools/asset_packer.cpp`
  - 做法：在现有资产拷贝之后，加一步：从 `runtime_dir` 拷所有 `*.dll` 和整个 `shaders/`
    到 `output_dir`；`copy_player` 为真时再拷 `arti_player.exe`（Windows）/ `arti_player`。
    复用文件里已有的 `copyOneFile()`。
    失败策略照 D5：缺 DLL 或 `shaders/` → `fail()`；缺 player → 只往 `report.errors`
    之外记一条 warn 日志，不影响 `succeeded`。
  - 验收：编译通过。
  - 结果：`copyRuntimeFiles()` 已实现：`*.dll` 通配 + `shaders/` 递归整目录 + 可选播放器。
    “没有 DLL”这条失败包了 `#if defined(_WIN32)` —— 非 Windows 上运行时依赖走 rpath，
    目录里本来就没 .dll。拷贝放在 pack 的最后一步，失败不会留下「有 exe 没资产」的半成品。

- [x] **4.3 main.cpp 接上**
  - 文件：`Tools/asset_tools/main.cpp`（pack 分支，第 119 行和第 153 行附近）
  - 做法：`options.runtime_dir = core::executableDir();`，并加两个开关：
    `--no-runtime`（清空 `runtime_dir`）和 `--no-player`（`copy_player = false`）。
    同步更新 `printUsage()`。
  - 验收：`asset_tools pack` 无参数时的用法输出里能看到新开关。
  - 结果：`options.runtime_dir = arti::core::executableDir()`，加了 `--no-runtime` / `--no-player`，
    warnings 打到 stderr，统计行多了 runtime file 数，`printUsage()` 同步。

- [x] **4.4 更新 `asset_packer.h` 的产物清单注释**
  - 文件：`Tools/asset_tools/asset_packer.h`
  - 做法：产物布局那段加上 `shaders/**` 和 `*.dll`（以及 `arti_player.exe`），
    并把「把 `arti_player.exe` 拷进 `<out>/` 就能双击运行」改成「产物即可直接运行」。
  - 验收：注释和实现一致。
  - 结果：产物清单加了 `shaders/**` / `*.dll` / `arti_player[.exe]`，「拷进去就能跑」改成
    「产物是自足的」。

### 阶段 5 · 端到端验收与文档

- [x] **5.1 干净构建 + 打包 + 异地运行**
  - 见下面「端到端验收」。这一步是整个任务的验收，**没过不许打勾**。
  - 结果：**通过。** 完整证据链：
    1. `rm -rf build` + configure + build（216 个目标）
    2. clean PATH 下 `asset_tools pack` → exit 0，`Packed 9 asset(s), 12 artifact(s),
       1 scene(s), 17 runtime file(s)`（3 DLL + 13 shader + 1 播放器）
    3. 把两条回落路径（`src/shaders`、`Tools/resources`）改名让它们失效，PATH 只剩
       `C:\Windows\system32;C:\Windows`，跑 `H:	mprtipackrti_player.exe --stats`
    4. 日志：`Loading shaders from 'H:	mprtipack\shaders' (staged next to the executable)`
       → `First frame rendered (4 draw calls)` → `ArtiChoco stopped`，exit 0，无 error
    5. 回落路径已恢复，无 `.moved` 残留
    附带：`ctest` 过；clean PATH 下直接跑 `asset_pipeline_smoke.exe` 也是 exit 0。
    偏差：文档原先写的是「整个源码树改名」，但 shell 的 cwd 在里面、Windows 上锁着改不了；
    改成只改名两个回落目录，证明力等价（它们就是宏指向的那两个地方）。

- [x] **5.2 更新架构文档**
  - 文件：`docs/Architecture/README.md`（§6 构建里「二进制不可搬移」那段、
    §5「项目（打包后）」的布局、§7「明确未做」）、
    `docs/Architecture/Applications.md`（§5 最后一段「拷出去的目录在没有源码树的机器上
    跑不起来」）
  - 做法：改成新的行为。「明确未做」里如果还有相关条目，一并订正。
  - 验收：`grep -rn "不可搬移" docs/` 没有过期残留。
  - 结果：`docs/Architecture/README.md`（§5 打包后布局、§6 两段查找与 DLL staging、§7 缺口表）、
    `Applications.md`（§5 整节重写）、`Assets.md`（§7 打包，补了新开关和失败策略）。
    缺口表里新增两条：非 Windows 的可搬移性未验、Release 发布未验（Debug CRT 不可分发）。
    `grep -rn "不可搬移" docs/` 已无过期残留。

- [x] **5.3 提交**
  - 见「风险与注意 · submodule 三层提交」。
  - 验收：`git submodule status` 里两个指针都指向新提交，且 `git status` 干净。
  - 结果：三层都已**本地提交**，由内向外：
    - ArtiChoco `e8f9006` fix(cmake): 修 slang 的运行时依赖配对，并补上动态加载的 slang-compiler.dll
    - ArtiRenderer `9265ddc` feat(shader): 着色器路径改成两段查找（含 ArtiChoco 指针）
    - ArtiEngine `23e66dd` feat(runtime): 构建产物与打包产物可搬移（含 ArtiRenderer 指针）
    - ArtiEngine `b135d9f` docs: 架构总览与可交接的任务清单
    `git status` 三个仓库都干净。
  - **未 push** —— 推送是对外动作，等用户点头。推的顺序必须和提交顺序一致（内 → 外），
    否则外层会指向一个别人拉不到的内层 commit：
    ```
    git -C ArtiRenderer/ArtiChoco push
    git -C ArtiRenderer push
    git push
    ```

---
### 阶段 6 · Release 发布验收（阶段 5 之后追加）

阶段 5 的可搬移性是在 **Debug** 下验的，而 Debug 产物不可发布：它链调试 CRT
（`ucrtbased.dll` / `MSVCP140D.dll` / `VCRUNTIME140D.dll`），那几个不在可再分发范围里，
本机能跑只是因为 System32 里装了。所以「可发布产物」这个交付物还没在它真正的用途上证明过。

这一阶段**先测量再决定**：CRT 怎么处理（静态链 `/MT` 还是随产物拷 redist）是个要拍板的
取舍，但只有在 Release 产物真的缺 CRT 时才需要拍 —— 所以 6.1～6.4 先跑完再看。

- [x] **6.1 Release 构建**
  - 命令：`cmake --preset release && cmake --build --preset release`（产物在 `build-release/`）
  - 验收：`build-release/bin/` 下有三个 exe，且 staging 拷到位 —— DLL 应该是**不带 d** 的
    （`SDL3.dll` / `slang.dll` / `slang-compiler.dll`），加 `shaders/`（13 个）和 `resources/`。
    这一条同时验证了 4.2 里「DLL 按通配拷、不硬编码文件名」在 Release 下也成立。
  - 结果：`build-release/bin/` 里的 DLL 恰好是不带 d 的那一套：`SDL3.dll` / `slang.dll` /
    `slang-compiler.dll`，加 `shaders/`（13）和 `resources/`。“DLL 按通配拷、不硬编码”在
    Release 下成立。

- [x] **6.2 看 Release 产物到底依赖什么**
  - 命令：`llvm-readobj --coff-imports build-release/bin/arti_player.exe | sed -n 's/^ *Name: *//p' | sort -u`
  - 做法：把导入表抄进交接区，重点看 CRT 那几个（`msvcp140` / `vcruntime140` /
    `vcruntime140_1` / `api-ms-win-crt-*`）以及有没有意外的新依赖。
  - 验收：交接区里有这份清单。
  - 结果：非 OS 依赖只剩 **三个 VC redist DLL**：`MSVCP140.dll`（0.61 MB）、
    `MSVCP140_ATOMIC_WAIT.dll`（0.06 MB）、`VCRUNTIME140.dll`（0.17 MB）。其余都不用担心：
    `api-ms-win-crt-*`（UCRT，Win10+ 自带）、`vulkan-1.dll`（显卡驱动装的）、
    `KERNEL32` / `USER32` / `SHELL32` / `IMM32`。Debug 下那个 `VCRUNTIME140_1D.dll` 在
    Release 导入表里没有。

- [x] **6.3 clean PATH 下打包**
  - 命令：`build-release/bin/asset_tools.exe pack projects/projects.artiproj <out> --overwrite`
  - 验收：exit 0，产物里 `*.dll` 是不带 d 的那一套，`shaders/` 13 个，`arti_player.exe` 在。
  - 结果：clean PATH 下 exit 0，`Packed 9 asset(s), 12 artifact(s), 1 scene(s),
    17 runtime file(s)`；产物里的 DLL 是不带 d 的三个，shaders 13 个，`arti_player.exe` 在。
    产物总大小 121 MB（其中 `slang-compiler.dll` 21.9 MB、`SDL3.dll` 3.6 MB）。

- [x] **6.4 把回落路径改名，clean PATH 跑产物**
  - 做法：照 5.1 的办法 —— 改名 `ArtiRenderer/ArtiRenderer/src/shaders` 和 `Tools/resources`，
    `$env:PATH` 只留 `C:\Windows\system32;C:\Windows`，跑产物的 `arti_player.exe --stats`，
    用 `$p.CloseMainWindow()` + `WaitForExit()` 让它干净退出（否则 spdlog 的缓冲丢日志）。
  - 验收：日志里有 `Loading shaders from '<out>\shaders' (staged next to the executable)` 和
    `First frame rendered (N draw calls)`，exit 0，无 error。**做完把两个目录改回来。**
  - 结果：**通过。** 两条回落路径改名失效 + PATH 只剩 `C:\Windows\system32;C:\Windows`，
    日志：`Loading shaders from 'H:	mprtipack-rel\shaders' (staged next to the executable)`
    → `First frame rendered (4 draw calls)` → `ArtiChoco stopped`，干净退出 exit 0，无 error。
    回落路径已恢复。

- [x] **6.5 判定 CRT 要不要处理**
  - 做法：6.4 过了说明**本机**的 System32 已经提供了 Release CRT。但那不等于「任何机器都行」
    —— `msvcp140.dll` / `vcruntime140*.dll` 靠 VC++ Redistributable，不是 Windows 自带。
    所以这一步的产出是一个判断，不是一次运行：产物要不要自带 CRT。
  - 结果：**用户拍板：动态链接（保持 `/MD`），把 redist DLL 随产物拷。** 实现见阶段 7。
    没选静态链 CRT，所以不动 `CMAKE_MSVC_RUNTIME_LIBRARY`，也不用担心 `/MT` 和 `/MD` 混用
    以及第三方静态库跟着重编。

- [x] **6.6 回写文档**
  - 文件：`docs/Architecture/README.md`（§7「明确未做」）、`docs/Architecture/Applications.md`（§5）
  - 结果：先按 6.4 的结果订正过一轮（Release 可搬移性已验，剩 CRT 未定），阶段 7 做完之后
    再订正成「产物自带 CRT」。

---

### 阶段 7 · 产物自带 CRT（6.5 定了「动态链接 + 随产物拷 redist」之后）

- [x] **7.1 新建 `ArtiMsvcRuntime.cmake`**
  - 文件：`ArtiRenderer/ArtiChoco/cmake/ArtiMsvcRuntime.cmake`（新建）
  - 做法：发现 CRT redist 目录并提供 `artichoco_stage_msvc_runtime(target)`，形状和
    `artichoco_stage_vulkan_sdk_runtime()` 一致。发现顺序：`ARTI_MSVC_REDIST_DIR`（显式）→
    `$ENV{VCToolsRedistDir}`（开发者命令提示符）→ `vswhere` 找 VS 安装位置再 glob
    `VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT`，自然序取最大。非 Windows 上函数是空操作。
  - 验收：配置期打出 `ArtiChoco MSVC CRT redist: <path> (N dll)`。
  - 结果：`H:/Visual Studio/Community/VC/Redist/MSVC/14.51.36231/x64/Microsoft.VC145.CRT
    (10 dll)`。
  - **没用 `InstallRequiredSystemLibraries`**：那个模块是 `if(MSVC)` 把门的，而本项目用独立
    clang 走 MSVC ABI（`CMAKE_CXX_COMPILER_FRONTEND_VARIANT` 是 `GNU`），此时 CMake 的 `MSVC`
    和 `MSVC_TOOLSET_VERSION **都是空的**，模块会静默什么都不做。单独建了个测试工程验过。
  - **找不到时 `message(WARNING)` 而不是 `FATAL_ERROR`**：缺 CRT 只影响产物能不能拿去别的
    机器，不影响本机构建和运行（能构建说明本机装着 CRT）。为一个打包问题让整个构建配不起来
    不划算。

- [x] **7.2 整个 CRT 目录都拷，不只挑导入表里那三个**
  - 验收：这个决定要有依据，不能只凭「反正不大」。
  - 结果：**有依据，而且是必须的。** 跑起来看进程实际加载的模块，`VCRUNTIME140_1.dll` 也被
    加载了 —— 它**不在 exe 的静态导入表里**，是 `MSVCP140.dll` 自己拉进来的。只拷那三个的话
    它会回落到 System32，在没装 redist 的机器上就是缺文件。多出来的七个共约 1.1 MB，
    产物本身百 MB 量级，代价可忽略。

- [x] **7.3 四个 target 加调用**
  - 文件：根 `CMakeLists.txt`（`include(ArtiMsvcRuntime)`）、`Runtime/player/CMakeLists.txt`、
    `Tools/scene_editor/CMakeLists.txt`、`Tools/asset_tools/CMakeLists.txt`（含
    `asset_pipeline_smoke`）
  - 验收：`build-release/bin/` 下出现 10 个 CRT DLL。
  - 结果：13 个 DLL（10 CRT + SDL3 + slang + slang-compiler），共 28 MB。

- [x] **7.4 pack 不用改**
  - 验收：`pack` 的产物里有 CRT DLL。
  - 结果：**确实不用改。** `copyRuntimeFiles()` 是「把 `runtime_dir` 下所有 `*.dll` 拷过去」，
    staging 一放进 `build-release/bin`，pack 就自动带上了 —— `27 runtime file(s)`
    （13 DLL + 13 shader + 1 播放器），比之前的 17 多了 10 个。当初「按通配拷、不硬编码文件名」
    这个决定在这里第二次回本（第一次是 Release 的不带 d 文件名）。

- [x] **7.5 验证产物真的在用自己那份 CRT**
  - 做法：光看「文件在不在」不够 —— System32 里也有同名文件，得确认加载的是哪一份。
    起进程之后读 `$p.Modules` 的 `FileName`。同时照 6.4 把两条回落路径改名、PATH 清干净。
  - 验收：CRT 模块的路径指向产物目录，且照常渲染。
  - 结果：**通过。**
    ```
    MSVCP140.dll              H:\tmp\artipack-crt\MSVCP140.dll
    MSVCP140_ATOMIC_WAIT.dll  H:\tmp\artipack-crt\MSVCP140_ATOMIC_WAIT.dll
    VCRUNTIME140.dll          H:\tmp\artipack-crt\VCRUNTIME140.dll
    VCRUNTIME140_1.dll        H:\tmp\artipack-crt\VCRUNTIME140_1.dll
    SDL3.dll / slang.dll / slang-compiler.DLL   都在产物目录
    ucrtbase.dll / msvcp_win.dll                System32（UCRT，Windows 自带，正确）
    ```
    日志：`Loading shaders from 'H:\tmp\artipack-crt\shaders' (staged next to the executable)`
    → `First frame rendered (4 draw calls)` → `ArtiChoco stopped`，干净退出 exit 0。
  - **仍然没验的**：真正一台没装 VC++ Redistributable 的机器。本机无法卸载 redist 来测，
    但「进程加载的是产物里那一份」已经是能拿到的最强证据 —— exe 所在目录的搜索优先级高于
    System32，而这几个 DLL 都不是 KnownDLLs。
- [x] **7.6 Debug 构建跳过 CRT staging**
  - 做法：第一版无条件拷，结果 `build/bin` 里也堆进了 10 个 release CRT DLL —— Debug exe 链的是
    调试 CRT（`MSVCP140D` / `VCRUNTIME140D` / `ucrtbased`），那 10 个**一个都不会被加载**。
    在函数里加一句 `if(CMAKE_BUILD_TYPE STREQUAL "Debug") return()`。
  - 为什么值得加这几行：留着的话输出目录里十个没人用的文件会让人以为 Debug 产物也是可发布的；
    更糟的是拿 Debug 构建去 pack 会得到「exe 要调试 CRT、包里装的是 release CRT」的组合，
    在别人机器上照样跑不起来，而包看起来是完整的。
  - 单配置生成器（本项目用 Ninja）配置期就知道 `CMAKE_BUILD_TYPE`；多配置生成器下它是空的、
    判断为假、照常拷 —— 那种情况下宁可多拷也不要少拷。
  - 验收：Debug 的 `build/bin` 只有 3 个 DLL（`SDL3d` / `slang` / `slang-compiler`），
    Release 的 `build-release/bin` 有 13 个。两边 `arti_player --help` 在 clean PATH 下都 exit 0。
  - 结果：如上，两边都过。`ctest` 在两个配置下都过。


---


## 端到端验收

全部阶段做完之后，按顺序跑一遍，每一条都要过：

```powershell
# 1. 干净构建 —— 不能依赖任何残留物
Remove-Item -Recurse -Force build
cmake --preset debug
cmake --build --preset debug

# 2. build/bin 自带运行时
#    期望：SDL3*.dll、slang*.dll、shaders/*.slang（13 个）、resources/fonts/...
ls build/bin
ls build/bin/shaders

# 3. 打包
build/bin/asset_tools.exe pack projects/projects.artiproj H:/tmp/artipack --overwrite

# 4. 产物自检
#    期望：<Name>.artiproj、catalog.artimanifest、Library/Artifacts/**、
#          Assets/**/*.artiscene、shaders/**、*.dll、arti_player.exe
ls H:/tmp/artipack
```

关键的一条 —— **模拟「另一台机器」**：

```powershell
# 5. 把源码树临时改名，让绝对路径宏一定失效
Rename-Item H:/CGLABH/Arti/ArtiEngine H:/CGLABH/Arti/ArtiEngine_moved

# 6. 在一个把 Vulkan SDK 从 PATH 里摘掉的干净环境里跑产物
$env:PATH = "C:\Windows\system32;C:\Windows"
H:/tmp/artipack/arti_player.exe --stats

# 7. 改回来
Rename-Item H:/CGLABH/Arti/ArtiEngine_moved H:/CGLABH/Arti/ArtiEngine
```

第 6 步必须：窗口起来、场景出图、`--stats` 覆盖层显示非零 draw calls。
只要它还在找源码树或报缺 DLL，这个任务就没完成。

> 第 5 步会让 `build/` 里的绝对路径全部失效，改回来之后建议再 `cmake --preset debug` 一次。
> 嫌麻烦的话可以换成把产物拷到一台没装 SDK 的机器 / 一个干净的容器里跑 —— 目的一样。

---

## 风险与注意

### submodule 三层提交

嵌套关系是 **ArtiEngine → ArtiRenderer → ArtiChoco**，三个都是独立 git 仓库，都在 `main` 上：

```
$ git submodule status
 0af620a ArtiRenderer (heads/main)
$ git -C ArtiRenderer submodule status
 ed0d2df ArtiChoco (heads/main)
```

本任务会改到全部三层（阶段 1 改 ArtiChoco，阶段 2 改 ArtiRenderer，阶段 2～5 改 ArtiEngine）。
提交顺序**必须由内向外**：

1. 在 `ArtiRenderer/ArtiChoco/` 里提交 → push
2. 在 `ArtiRenderer/` 里 `git add ArtiChoco` 提交指针 + 自己的改动 → push
3. 在仓库根 `git add ArtiRenderer` 提交指针 + 自己的改动

历史上这类指针提交用的是 `chore(deps): 推进 ArtiRenderer 到 <sha>` 这个格式，沿用它。

**顺序搞错的后果**：外层指向一个没 push 的内层 commit，别人 `git submodule update` 会拉不到。

### 别对既有文件跑 clang-format

仓库根有 `.clang-format`，但**既有文件没有按它格式化过**。对老文件跑 `clang-format -i` 会
产生大量与改动无关的噪声（行尾注释从 2 空格被压成 1、构造函数初始化列表被拆行、既有调用被
重新折行）。**改既有文件时手写成周围的风格**（4 空格、100 列、行尾注释保持原有对齐）。
新建的文件（阶段 1 的 `io/paths.{h,cpp}`）可以跑。行尾一律 LF。

### Debug / Release 的 DLL 文件名不同

Debug 是 `SDL3d.dll` / `slangd.dll`，Release 是 `SDL3.dll` / `slang.dll`
（`ArtiVulkanSDK.cmake` 里 `IMPORTED_LOCATION_DEBUG` / `_RELEASE` 分开配的）。
所以阶段 4 的 pack **必须按通配 `*.dll` 拷，不能硬编码文件名**。
`$<TARGET_RUNTIME_DLLS>` 自己会按配置选对，那一侧不用管。

### `$<TARGET_RUNTIME_DLLS>` 的前提

它只认 `SHARED IMPORTED` 且设了 `IMPORTED_LOCATION` 的 target。Windows 分支里
`arti_sdk_sdl3` / `arti_sdk_slang` 正是这么声明的（`ArtiVulkanSDK.cmake:143` 和 `:181`），
所以能工作。**非 Windows 分支它们是 `UNKNOWN IMPORTED`**，那条路上
`artichoco_stage_vulkan_sdk_runtime()` 走的是设置 `BUILD_RPATH`，不拷文件 —— 本任务只在
Windows 上验收，Linux 的可搬移性不在范围内。

### 不在本任务范围内

- 着色器的离线预编译（现在是运行期 Slang → SPIR-V。要做成产物里放 `.spv` 是另一件事，
  会牵动 `SlangCompiler` 和反射链路）
- 编辑器里的「Build」菜单入口（打包目前只有 CLI）
- Linux / macOS 的可搬移性
- `arti_player` 的 Release 构建验收（本任务在 Debug 下验收即可，但 pack 的通配拷贝要保证
  Release 下也对）
