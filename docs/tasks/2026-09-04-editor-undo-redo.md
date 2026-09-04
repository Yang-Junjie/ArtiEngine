# 编辑器的 Undo / Redo

| | |
| --- | --- |
| **状态** | 已完成 |
| **创建** | 2026-09-04 |
| **最后更新** | 2026-09-04 |
| **涉及仓库** | **只有 ArtiEngine。** 不动 ArtiRenderer、不动 ArtiChoco，所以**不推任何 submodule 指针** —— `SceneSerializer::serialize` / `deserialize` 本来就是公开的（`scene_serializer.h:15-16`），够用 |
| **目标** | `Ctrl+Z` / `Ctrl+Y` 真能用，Edit 菜单那两项不再恒灰。做法是**场景序列化文本的快照栈**：一次交互一条历史项，选中一起恢复。顺带把脏标记从「两处手写」改成由历史精确推导 |
| **明确不做** | 不做命令模式 / 逐字段 diff（D1）。菜单里不显示「Undo Move」这种操作名（见「待定」）。历史不跨场景切换（换场景清空）。Simulate / Play 期间不记历史、不许撤销（D5）。不做资产 / 项目设置的撤销 —— 只管场景 |

---

## 交接区

> **全文唯一允许改动的段落。每次收工前更新这里。**

**当前进度：已完成。** 五个阶段的代码、文档、测试和人工验收全部过了。

**结论：这套设计成立，而且成立的方式值得记下来 —— 真正的收益不是「撤销做出来了」，而是
「变更检测精确到不可能出假历史项」让接入成本从 42 个控件降到 0 个。** `inspector_panel.cpp`
除了修那个预存在的缓存 bug，一行都没为撤销改过。将来加组件、加字段也一样不用管撤销。

验收的分工是这么落的：
- **自动化**（`ctest` 10 条，含两条新的）：序列化的规范形式、场景 ↔ 文本的往返、
  「一次拖拽一条历史项」「空提交不进栈」「上限从最旧一端丢」「删除撤销后选中回来」这些纯逻辑。
  两处反向验证都真看到了红。
- **人工**（用户 2026-09-04 在编辑器里）：阶段 3.4 的七条 + 4.2 那条 Environment，全部没问题。
  人工只需要确认「信号真的按预期到达」，逻辑那一半已经被测试钉住了。

代码指针没有推 —— **这次一个 submodule 都没动**（`SceneSerializer::serialize` / `deserialize`
本来就是公开的，够用）。

提交：`e48ce1a`（`World` 的内存快照 + `scene_snapshot_smoke`）、`a586b44`（Environment 缓存的
预存在 bug）、`f92df82`（`EditHistory` + 编辑器接线 + 脏标记 + 文档）。

### 留给下一个人的三条

1. **「撤销对某个字段不生效」的第一嫌疑人是面板的缓存，不是历史栈。** `a586b44` 修的就是这个：
   Inspector 里几个 UUID 输入框每帧都把缓存的文本写回组件，缓存不跟组件对账就会把撤销吃掉。
   加这类控件时照 MeshRenderer 的样子做。
2. **只有序列化过的组件能撤销。** 现在八个引擎组件两边都注册了所以没差别，但
   `Scene.md:217` 说「可拷贝但不持久化」的运行时组件是允许存在的 —— 那种组件撤不回来且不报错。
   这条已经写进 `Scene.md` 加组件那张清单旁边了。
3. **恢复一次快照之后实体的遍历顺序会变成 UUID 序。** 写测试别写「改第一个实体」（踩过一次）；
   编辑器里的表现是刚 `Create Empty Entity` 建出来的实体撤销之后可能在 Hierarchy 里换位置，
   和存盘再读回来是同一种副作用。

### 一个不在本任务范围内的发现（还没修）

`editor_layer.cpp` 工具栏那行写的是 `"| %s (Alt+Q/W/E)"`，但 `EditorGizmo::handleShortcuts()`
接的是光秃秃的 `W` / `E` / `R`（`editor_gizmo.cpp:47-55`），`Applications.md` 也是 `W/E/R`。
那行标签是错的。**本任务刻意没改** —— 和撤销无关，混进来只会让 diff 变浑。

### 和计划的偏差（都是做的时候发现计划有洞）

1. **多了一个 ctest：`edit_history_test`**（`Tools/scene_editor/tests/`，把 `src/edit_history.cpp`
   直接编进测试目标 —— 编辑器是可执行不是库）。原计划里阶段 2 / 3 只有人工验收，但「一次拖拽只
   产生一条历史项」「空提交不进栈」「上限从最旧一端丢」这些是**纯逻辑**，没道理只靠手点。
   它顺带守着一条约束：`edit_history.cpp` **不许依赖 `Application` 单例**（测试里没有
   `Application`，取一下就断言）。
2. 为了第 1 条，`edit_history.cpp` 里去掉了「丢弃坏历史项」那条 error 日志 —— 它是**多余**的，
   失败的原因 `World::restoreScene()` 自己已经记过了。这不是为测试让步，是顺手删了一行重复。
3. `kMaxEntries` / `kMaxBytes` 从 `.cpp` 挪到头文件当公开常量：上限是这个类的契约的一部分，
   而且测试要拿它算「撤到底应该停在哪一条」。
4. **4.1 和 2.3 一起做的**：它们改的是 `scene_document.cpp` 里同样那四处
   （`m_dirty = false` 的位置正好就是要重取撤销基线的位置），分两次改等于把同一段代码动两遍。
5. **顺带让脏标记可见**（工具栏 `[Edit]` 后面一个琥珀色 `*`）。原计划说 4.1 的验收「用调试器 /
   临时打一行看，验收完删掉」—— 但脏标记现在是**精确**的了，值得留着：一个看不见的标记没人会
   维护，而且以前它摆出来纯属误导（只有一处置位），现在才配得上显示。
6. `SceneDocument::markDirty()` 保留但**目前没有调用方**。场景内的改动由历史推导，不需要报到；
   留着是给场景之外的改动（项目设置）用的，那条还没接。

### 一个写测试的坑（踩过一次，浪费了一轮）

**恢复一次快照之后，实体的遍历顺序会变成 UUID 序**（`deserialize` 按文件顺序建实体，而文件是按
UUID 排的）。所以测试里千万别写「改第一个实体」—— 往返前后「第一个」不是同一个实体。第一版就
这么写的，症状是 `redo` 之后名字对不上，从现象看完全像是历史栈的 bug。**按 UUID 指名改。**

这条对编辑器本身也成立：`Hierarchy` 面板列根节点是按 `view<IDComponent>` 遍历的，所以刚
`Create Empty Entity` 建出来的实体（追加在末尾）在一次撤销 / 重做之后可能换位置。和存盘再读回来
是同一种副作用，接受。

### 反向验证做过两次，都看到了红色

1. 去掉 `scene_serializer.cpp` 按 UUID 的排序 → `canonicalFormHolds` 红，而「连续两次 capture 相同」
   **全绿**（详见下面阶段 1 的记录）。改的是 submodule，验完 `git checkout --` 还原了。
2. 去掉 `commitIfChanged` 里 `text == m_current.text` 那一半 → 「场景没变却压了一条历史项」红。
   这条是 D2 的核心，必须验。

顺带确认了一件对 D2 有利的事：yaml-cpp 的 emitter 默认精度足够让 float 往返，所以
「capture → restore → capture」是逐字节相同的，不是「差最后一位但看不出来」。

开工前已经查清、不用再查的事（都在「背景与现状」里带证据）：

- 序列化输出是**规范形式**（实体按 UUID 排、组件按类型名排），所以「文本相同 ⇔ 场景相同」成立。
  这是 D2 整个设计的地基，先确认了才敢用字符串比较当变更检测。
- 场景的写入点有 **42 处以上**，全部是直接改组件引用。这是不做命令模式的原因（D1）。
- 脏标记现在只在 2 处置位，也就是说 Inspector 改一个数、拖一下 gizmo、建 / 删 / 复制实体
  **都不会**让标题栏或菜单知道场景脏了。D8 顺带修掉这个。
- 发现一个**预存在的 bug**，它会让 undo 在某个字段上静默失效，所以必须在本任务里修（D9）。

### 一条构建环境的坑（前两个任务都撞过）

**`ninja` 和 `clang` 都不在普通 shell 的 PATH 上。** 它们在 VS 18 Community 里：

```
/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja
/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin
```

平时 `cmake --build --preset debug` 能跑是因为它不需要重跑 configure。**本任务的 1.3 和 2.1 都会
改 `CMakeLists.txt`**（加测试目标、加源文件），那会触发 re-configure，找不到 `ninja` 就把
`CMakeCache.txt` 里的编译器打回 `UNINITIALIZED`。**先把上面两个目录 prepend 到 PATH 再动 CMake。**

### 一个不在本任务范围内的发现

`editor_layer.cpp:467` 那行工具栏文字写的是 `"| %s (Alt+Q/W/E)"`，但 `EditorGizmo::handleShortcuts()`
接的是**光秃秃的 `W` / `E` / `R`**（`editor_gizmo.cpp:47-55`），架构文档 `Applications.md:137` 也是
`W` / `E` / `R`。那行标签是错的。**本任务不改** —— 它和撤销无关，混进来只会让 diff 变浑。

---

## 背景与现状

### 证据 1：撤销完全不存在，而且是刻意留白的

```
$ grep -rn "undo\|Undo\|redo\|Redo" --include=*.h --include=*.cpp ArtiEngine Runtime Tools
Tools/scene_editor/src/editor_layer.cpp:322:  if (ImGui::MenuItem("Undo", "Ctrl+Z", false, false)) {
Tools/scene_editor/src/editor_layer.cpp:324:  if (ImGui::MenuItem("Redo", "Ctrl+Y", false, false)) {
Tools/scene_editor/src/editor_layer.cpp:377:  // ...而且没有 undo 能救。
Tools/scene_editor/src/editor_layer.cpp:423:  // Ctrl+Z / Ctrl+Y **刻意不接**：没有 undo 栈，接上去只能是个什么都不做的键。
```

两个空的菜单项（第四个参数 `enabled=false`，所以恒灰）、两条注释。没有栈、没有快照、没有命令。

`Applications.md:146` 还记了一条代价：`Del` 删实体「那一下不可撤销」，所以 `Delete` 的键位除了
`Shortcut()` 的焦点路由，还得再显式挡一道 `WantTextInput`。**撤销做出来之后那道防线的必要性会下降，
但不要顺手拆掉它** —— 误删一个实体然后按撤销，仍然比根本没删好。

### 证据 2：场景的写入点有 42 处以上，全是直接改组件引用

```
$ grep -c "draw\(Bool\|Float\|Color\|Enum\|Vec3\)" Tools/scene_editor/src/panels/inspector_panel.cpp
42
```

这 42 行的形状都是 `drawFloatRow("Intensity", &light->intensity, ...)` —— 把组件字段的地址交给
ImGui，控件当场改掉它。**没有一个中间层可以插钩子。** 另外还有：

| 写入点 | 位置 |
| --- | --- |
| gizmo 拖拽写回 transform | `editor_gizmo.cpp:95-96` |
| 建实体 / 建子实体 | `hierarchy_panel.cpp:100`、`:158` |
| 复制 / 删除（延迟到下一帧执行） | `hierarchy_panel.cpp:68`、`:78` |
| 改名 | `inspector_panel.cpp:264-266` |
| 加 / 删组件 | `inspector_panel.cpp:160`、`:174` |
| 材质槽增删 | `inspector_panel.cpp:379-399` |
| 拖资产进 Viewport 生成实体 | `editor_layer.cpp:528`、`:551` |
| 聚光灯内外锥角的钳制（**每帧无条件写**） | `inspector_panel.cpp:466` |
| 碰撞体半长的钳制（**每帧无条件写**） | `inspector_panel.cpp:549-551` |

最后两行值得单独记一句：它们每帧都写，只是通常写的是同一个值。这决定了变更检测**不能**靠
「这一帧有没有人写过组件」，只能靠比较结果（D2）。

**没有拖拽改父子。** `hierarchy_panel.cpp` 里改层级只有右键菜单的 `Create Child Entity`
（`:157-160`）一条路，没有 `BeginDragDropTarget`。所以改动点比「一个实体树面板」听起来的少。

### 证据 3：序列化输出是规范形式 —— 「文本相同 ⇔ 场景相同」成立

`scene_serializer.cpp:36-39` 把实体**按 UUID 排序**：

```cpp
std::ranges::sort(entity_handles, [&scene_registry](entt::entity lhs, entt::entity rhs) {
    return scene_registry.get<IDComponent>(lhs).id.value() <
           scene_registry.get<IDComponent>(rhs).id.value();
});
```

`:46-48` 把组件类型**按类型名排序**：

```cpp
std::ranges::sort(serialization_entries, [](const auto* lhs, const auto* rhs) {
    return lhs->typeName() < rhs->typeName();
});
```

所以输出既不依赖 EnTT 的 storage 顺序，也不依赖注册顺序 —— 同一个场景无论怎么来的，dump 出来
都是同一段文本。**这是 D2 的地基**：没有这一条，字符串比较就会因为顺序抖动而产生一堆假历史项。

反过来也成立的一面：`deserialize` 按文件顺序建实体（`:186-188`），而文件顺序是 UUID 序，所以
**一次撤销之后实体的遍历顺序会变成 UUID 序**。Hierarchy 面板按 `view<IDComponent>` 遍历列根节点
（`hierarchy_panel.cpp:108-113`），所以刚 `Create Empty Entity` 建出来的实体（追加在末尾）在撤销 /
重做之后可能换位置。**这是可见的副作用，但和存盘再读回来是同一种**，接受。

### 证据 4：整场景替换是安全的 —— 所有跨帧引用都是 UUID

| 谁 | 存的是什么 | 整场景替换后 |
| --- | --- | --- |
| `EditorContext::m_selected_entity` | `optional<UUID>`（`editor_context.h:77`） | 活的（UUID 由序列化保留） |
| 拾取 id 表 | `UUID ↔ uint32_t`（`Scene.md` 第 4 节） | 活的 |
| gizmo / Inspector / Hierarchy | 每帧 `findEntity(uuid)` 重查 | 活的 |
| Inspector 的 UUID 输入框缓存 | 按实体 UUID 存的 `static` map | **有一个不会自愈，见证据 6** |

没有任何地方跨帧存 `entt::entity` 或组件指针。所以「把整个实体存储换掉」不会留下悬空引用 ——
这也是 Play 模式的快照（`editor_context.cpp:41`、`:55`）已经在做的事。

### 证据 5：脏标记现在基本是坏的

```
$ grep -rn "markDirty" --include=*.h --include=*.cpp ArtiEngine Runtime Tools
Tools/scene_editor/src/editor_layer.cpp:535:        m_document->markDirty();
Tools/scene_editor/src/editor_layer.cpp:589:        m_document->markDirty();
Tools/scene_editor/src/scene_document.h:44:    void markDirty() noexcept { m_dirty = true; }
```

**只有拖资产进 Viewport 这一条路会置位。** Inspector 改数、拖 gizmo、建 / 删 / 复制实体、改名 ——
全都不置位。`scene_document.h:41-42` 自己写了原因：「不做逐字段脏检查 —— 那需要在 Inspector 和
gizmo 的每个写入点埋钩子，先做个够用的版本」。

本任务做完之后，那个钩子就不用埋了：历史栈**已经**在算「场景变了没有」，脏标记直接读它就行（D8）。

### 证据 6：Environment 的 UUID 输入框缓存永不失效，会把撤销吃掉

`inspector_panel.cpp:481-494`：

```cpp
auto& text = environmentEditorStates()[entity.getComponent<scene::IDComponent>().id];
if (text.empty()) {                                    // ← 只在第一次初始化
    text = uuidToText(environment->equirect_texture.id());
}
...
core::UUID applied{};
if (drawUuidInput("##equirect_uuid", text, applied)) {  // ← 只要 text 能解析就返回 true
    environment->equirect_texture = AssetHandle<TextureAsset>{ applied };
}
```

`drawUuidInput`（`ui_widgets.h:134-147`）**不是**「变了才返回 true」，而是「能解析就返回 true」——
所以这个组件每帧都被写一次。而 `text` 只在第一次为空时同步，之后**再也不跟组件对账**。

后果：撤销一次「改 Equirect Texture」，下一帧 Inspector 就把缓存里的旧文本写回组件，撤销**静默失效**
（只要那个 Environment 实体还选中着）。

对照组：MeshRenderer 那边是对的（`:339-342`），条件是 `mesh.id() != state.mesh_applied` ——
组件变了就重新同步。**Environment 缺的就是这个 `applied` 字段。** 见 D9。

---

## 设计决定

### D1 · 快照式，不做命令模式 —— 已定

命令模式（每个可撤销动作一个对象，自带 do / undo）粒度更细、内存更省，是「正确」的做法。
**但对这个编辑器它的接入成本是 42 个控件 × 一个命令类**（证据 2），而且每加一个组件字段都要
记得再加一个。漏一个的症状是「这个字段撤不回来」，而且只有手动试到那个字段才会发现。

快照式的接入成本和字段数**无关**：控件照原样直接改组件，历史层只在交互结束时看一眼「场景变了没」。
代价写清楚：

- **粒度是整个场景**，不是「这一个字段」。撤销一次会把那次交互期间的所有改动一起撤掉 ——
  对编辑器来说这正是想要的（一次拖拽 = 一条历史项）。
- **每条历史项是整个场景的一份拷贝**，内存和提交耗时都是 O(场景)。上限见 D7。

### D2 · 快照存**序列化文本**，不存 `Scene` 克隆 —— 已定

两条路都可行：`Scene::copyEntitiesFrom()`（Play 模式快照走的那条，`editor_context.cpp:41`）更快、
更省；`SceneSerializer` 的文本更慢、更占。**选文本，决定性的理由是变更检测：**

字符串比较让「场景到底变了没有」变成一行代码，而且是**精确**的。由此得到两个连锁的好处：

1. **不可能产生空历史项。** 点一下不改任何东西的控件、每帧无条件重写同一个值的钳制代码
   （证据 2 最后两行）—— 都不会污染历史栈。「按了 Ctrl+Z 但画面没反应」这类 bug 从根上不存在。
2. **因此提交时机允许写得粗。** 这才是真正的收益：不用在 42 个控件上分别判断「这个控件这一帧
   到底改了没有」，只要在「可能改了」的时候查一次即可（D3）。整个 `inspector_panel.cpp`
   **一行都不用改**（除了 D9 那个预存在的 bug）。

用克隆的话，要么放弃变更检测（于是要在每个控件上精确判断，退回 D1 的成本），要么实现「两个
Scene 相等吗」（那等于自己写一遍序列化，还更容易漏字段）。

附带的两个好处：`deserialize` 的恢复路径**自带校验**（先建 staging 场景，查必需组件 / UUID 唯一 /
父引用有效 / 层级无环，通过了才替换 —— `scene_serializer.cpp:185-203`）；历史项是人能读的文本，
出问题能直接打出来 diff。

代价，写下来：

- **只有序列化过的组件能撤销。** 现在八个引擎组件既注册了拷贝也注册了序列化（`Scene.md` 第 6 节
  要求两者都登记），所以此刻没有差别。**将来若出现「可拷贝但不持久化」的运行时组件，
  它就撤不回来** —— `Scene.md:217` 明确说了那种组件是允许存在的。
- 提交一次要序列化 + emit 一遍全场景，比克隆慢。只在交互边界发生，不在每帧（D3）。

### D3 · 提交时机 = 交互结束的下降沿 + 显式请求，帧末统一查一次 —— 已定

```
帧初  history.beginFrame(选中)        ← 记下这一帧开始时的选中（D6）
      ... 面板照原样画，随便改场景 ...
帧末  if (交互刚结束 || 有人显式请求) history.commitIfChanged(world, 选中)
```

「交互刚结束」= 下面任一的**下降沿**（这一帧 false、上一帧 true）：

| 信号 | 覆盖 |
| --- | --- |
| `ImGui::IsAnyItemActive()` | 所有 Inspector 控件、改名、菜单项、按钮、下拉框 —— 一次拖拽从按下到松开 `ActiveId` 一直在，松开那帧掉沿，所以**整条拖拽合成一条历史项** |
| `EditorGizmo::isUsing()` | gizmo 拖拽（它不是 ImGui 的 item，`ImGuizmo` 自己管状态） |

「显式请求」= `history.requestCommit()`，只给**不经过 ImGui 控件**的改动用：

| 显式请求点 | 为什么需要 |
| --- | --- |
| `Ctrl+D` / `Del` 快捷键 | 键盘事件不激活任何 item，没有下降沿 |
| `HierarchyPanel` 落地 `m_pending_duplicate` / `m_pending_delete` | 请求在第 N 帧下，改动在第 N+1 帧才执行（`hierarchy_panel.cpp:62-88`），那一帧可能什么 item 都没动过 |
| `Create Empty Entity` / `Create Child Entity` | 菜单项本身有下降沿，但它在**弹出层**里，多要一条请求比推理弹出层的 ID 生命周期便宜 |
| `spawnAssetEntity()`（拖资产进 Viewport） | 拖放的结束是 `IsMouseReleased`，不是 item 下降沿 |

**为什么不是「每帧都查」：** 那会在每帧序列化整个场景。**为什么不是「上升沿时先存一份 before」：**
那要求「交互开始的那一帧还没改过场景」，对 `DragFloat` 和 `ImGuizmo` 恰好成立（按下那帧鼠标位移
为 0），但这是靠两个第三方库的内部行为撑着的前提，写不进任何断言。下降沿 + 比较不需要这个前提。

代价：一次不改任何东西的点击（比如点 UUID 那行复制到剪贴板，`inspector_panel.cpp:272-276`）会
白序列化一次全场景。可以接受，而且**这个代价是可测的** —— 见端到端验收第 6 条。

### D4 · `captureScene()` / `restoreScene()` 放 `World` —— 已定

`World` 已经持有 serializer（`world.h:54-55`，私有），而且 `loadScene` / `saveScene` 就在那儿。
在旁边加一对「场景 ↔ 内存文本」是最小的改动，也让这个能力能被**不带编辑器**地测试
（阶段 1.3 的 ctest 靠这一条成立）。

```cpp
// 把当前场景序列化成文本（和 saveScene 写进文件的是同一份内容）。失败返回空串。
std::string captureScene() const;
// 用 captureScene() 的文本替换当前场景。失败时**场景不变**，返回 false。
bool restoreScene(std::string_view text);
```

三个刻意的取舍：

- **返回 / 接受 `std::string`，不是 `YAML::Node`。** 头文件不用拖 yaml-cpp；而且文本才是能比较、
  能哈希、能打印的形式（D2）。
- **失败时场景不变**，和 `loadScene` 刻意不同（那个失败时会 `clearEntities()`，见
  `world.cpp:36`）。理由：读文件失败意味着「你要的那个场景不存在」，留半个更糟；而恢复一条历史项
  失败意味着**历史栈坏了**，这时候把用户正在编辑的场景清空是纯粹的雪上加霜。这个性质是免费的 ——
  `deserialize` 先建 staging 再 `replaceWith`（`scene_serializer.cpp:185-203`），抛在替换之前。
- **不动时钟。** `loadScene` 会 `resetClock()`，因为换场景后上一个场景的固定步长余额没有意义。
  恢复历史项不换场景，而且只在 Edit 模式可用（D5），时钟根本没在跑。归零反而会让物理在下一次
  进 Simulate 时多重建一次世界（帧号回退是它的重建信号，`Scene.md:143-146`）—— 无害，但没必要。

### D5 · 只在 Edit 模式记历史、只在 Edit 模式能撤销 —— 已定

和 `canSaveScene()` 完全同一条理由（`editor_layer.cpp:374-382`）：Simulate / Play 期间物理每个固定步
都在往 transform 里写。如果那期间提交历史，栈会被几十条「盒子又掉了 2 厘米」灌满；而撤销一下会
和正在跑的物理直接对打。

而且**现有语义已经覆盖了这个需求**：模拟期间的改动本来就落在快照上，Stop 就原样回来
（`Applications.md:104`「模拟一下再撤销是免费的」）。所以模拟期间不记历史不是缺功能，是不重复造。

实现上就是两个判断都挂在 `isSimulating()` 上，和 `canSaveScene()` 共用同一条轴 ——
**别新造一个 `isEditing()`**，`Applications.md:83-91` 专门讲过为什么不要再加含混的模式查询。

另外，**交互进行中不许撤销**（`IsAnyItemActive()` 或 gizmo 正在用时 `canUndo()` 返回 false）：
一边按着拖动框一边按 Ctrl+Z，ImGui 的 `ActiveId` 还指着那个控件，而它背后的组件已经被整个换掉了。
挡住这一下比推理那之后会发生什么便宜得多。

### D6 · 选中一起进历史，取「提交那一帧开始时」的选中 —— 已定

每条历史项 = `{ 场景文本, 选中的实体 }`。撤销 / 重做把两者一起恢复，恢复后那个实体不在了就清空选中。

**选中变化本身不产生历史项** —— 这是 D2 的比较白送的：点来点去文本不变，就没有历史项。

为什么取「帧初」而不是「帧末」的选中：删除是在 `HierarchyPanel::draw()` 开头落地的，落地时会
把选中清掉（`hierarchy_panel.cpp:83-86`）。帧末取的话，「删掉 E」这条历史项记下来的选中是**空**，
撤销回去 E 回来了但没被选中；帧初取的话记的是 `E`，撤销回去 E 直接选上。后者才是想要的。

### D7 · 栈上限：条数 + 字节预算双限 —— 已定

`64` 条 **且** `64 MiB`，超了从最旧的一端丢。

只限条数会在大场景上炸内存：5000 个实体的 dump 是 MB 级，64 条就是几百 MB。只限字节则在小场景上
让历史长得没必要（一个默认场景能存几千条，而没人会连按 Ctrl+Z 一千次）。两条一起才是「小场景够用、
大场景不炸」。丢弃从最旧一端走，重做栈不设独立上限（它的长度天然被撤销次数夹住）。

### D8 · 脏标记改成由历史推导 —— 已定

给每个状态一个自增 id，`SceneDocument` 记下「存盘时是哪个 id」，`isDirty()` = 当前 id ≠ 存盘 id。

比现在的 `bool m_dirty` 严格更准（证据 5：现在 42 个字段改了都不算脏），而且**撤销回到存盘时的
那个状态会自动变回「干净」** —— 这是 id 方案而不是「提交就置脏」的唯一理由，也是它值得的地方。

`markDirty()` 保留：`spawnAssetEntity` 那两处调用不删（多置一次脏是无害的），而且将来非场景的
改动（项目设置）可能还要用它。

### D9 · 顺手修 Environment 的 UUID 缓存 —— 已定

证据 6 那个 bug 会让撤销在 Equirect Texture 这个字段上**静默失效**，所以它不是「顺带清理」，
而是本任务的前置。修法照 MeshRenderer 的样子（`inspector_panel.cpp:339-342`）：给缓存加一个
`applied` 字段，组件的值和它不一致时重新同步。

### 待定：菜单里要不要显示操作名

「Undo Move Cube」比「Undo」好用。但快照式拿不到语义 —— 文本 diff 只能说「变了」，说不出「移动了」。
可行的做法是让**显式请求**那一侧带一个标签（`requestCommit("Delete Entity")`），下降沿那一侧统一
叫「Edit」。半吊子的标签比没有标签更让人困惑，**倾向先不做**，等真的觉得不够用再说。

---

## 任务清单

五个阶段。阶段 1 结束时有一个能过的 ctest 但编辑器行为完全没变；阶段 3 结束时 `Ctrl+Z` 真的能用。

### 阶段 1 · `World` 的内存快照（不碰编辑器）

**阶段 1 的记录**：「连续两次 `captureScene()` 文本相同」这条断言**抓不到「序列化没排序」这个
bug** —— 一次进程里不动场景的话，就算完全不排序，两次 dump 也一样，它测的是「稳定」不是
「规范」。而撤销栈依赖的是后者。所以补了 `canonicalFormHolds()`（两个内容相同、创建顺序相反的
`World`，dump 必须逐字节相同）。反向验证和这个诊断完全对上：排序删掉之后 `canonicalFormHolds`
红了，「连续两次相同」那条一动不动地全绿。**教训和视锥剔除那次是同一个。**

- [x] **1.1 `captureScene()` / `restoreScene()`**（D4）
  - 文件：`ArtiEngine/runtime/world.h`、`world.cpp`
  - 做法：`captureScene()` 走 `m_serializer->serialize()` + `YAML::Emitter`（照
    `asset/loaders/material_loader.cpp:60` 的写法），返回 `emitter.c_str()`；emitter 不 good 就记
    error 返回空串。`restoreScene()` 走 `YAML::Load(text)` + `m_serializer->deserialize()`，
    整个包在 try 里，失败记 error 返回 false、**不清场景**。
  - 注释里写下 D4 那三条取舍，尤其「失败时不清场景，和 loadScene 刻意不同」和「不动时钟」。
  - 验收：见 1.3。
  - **已完成。**`YAML::Load` 没有 `string_view` 的重载，`restoreScene` 里必须实体化一份
    `std::string`（代码里注了一句）。

- [x] **1.2 `scene_snapshot_smoke` 测试**
  - 文件：新增 `ArtiEngine/runtime/tests/scene_snapshot_smoke.cpp`
  - 做法：照 `physics_smoke.cpp` 的形状（`require()` + 退出码即结果，不引入测试框架）。
    建一个 `World`，摆几个实体（带父子、带各种组件、带缩放和旋转），断言：
    1. **幂等**：`captureScene()` 连调两次，两段文本**逐字节相同**。
    2. **往返**：capture → 改场景（删一个、加一个、改一个数）→ restore → 再 capture，
       结果和第一次的文本**逐字节相同**。
    3. **UUID 保留**：restore 之后原来那些 UUID 都还 `findEntity` 得到。
    4. **父子保留**：restore 之后 `getParent()` 关系和原来一致，`getWorldTransform()` 也一致
       （证明 `updateWorldTransforms()` 在恢复路径上跑过 —— `scene_serializer.cpp:201`）。
    5. **失败不清场景**：`restoreScene("这不是 YAML: [")` 和 `restoreScene("Entities: 3")`
       都返回 false，且**实体数一个没少**。
    6. **空场景往返**：实体数为 0 的场景也能 capture / restore，不崩。
  - 验收：见 1.3。
  - **已完成，比原计划多了两条**，都是做的时候发现原计划有洞：
    - **`canonicalFormHolds()`** —— 两个内容相同、创建顺序相反的 `World`，dump 必须逐字节相同。
      原因见交接区：第 1 条断言测的是「稳定」不是「规范」，抓不到没排序这个 bug。
      **这条才是 D2 的地基，别删。**
    - **「改了场景之后文本必须变」** —— 第 2 条的对照组。少了它，一个恒返回同一段文本的
      `captureScene()` 也能让整个测试全绿。
    - 世界变换的比较用 1e-5 的容差，不用精确相等：浮点要过一遍十进制文本，原理上就允许差
      最后一位。真丢了字段差的是量级，容差挡不住。
    - 夹具里每个字段都**刻意不取默认值** —— 取默认值的话「序列化漏写了这个字段」会被反序列化
      时的默认值悄悄补回来，往返测试什么都抓不到。
    - 第 5 组加了第三个坏输入（把 `arti.tag` 换成 `arti.nope`）：前两个连 YAML 都不成立，
      这个是「YAML 解析得动、内容过不了校验」，走的是另一段代码。

- [x] **1.3 注册进 ctest**
  - 文件：`ArtiEngine/CMakeLists.txt` 的 `if(BUILD_TESTING)` 块
  - 做法：照 `physics_smoke` 那几行加一个可执行，**链 `ArtiEngine::Runtime`**（不像 physics_smoke
    那样只链 box3d —— 这个测试的对象就是引擎自己）。`artichoco_stage_msvc_runtime()` 要加，
    Vulkan SDK 的 staging 不用（它不建 `RenderDevice`）。
  - **改 CMakeLists 会触发 re-configure**，先按交接区那条补 PATH。
  - 验收：`ctest` 从 **8** 条变 **9** 条，全绿。**并且反向验证一次**：把序列化里按 UUID 的排序
    去掉，`canonicalFormHolds` 那条必须变红 —— 证明这个测试不是空转的。
  - **已完成。**9/9 全绿；反向验证做过，见交接区。原计划写的是「7 条变 8 条」——
    数错了，基线是 8 条（漏数了 `scene_duplicate_test`）。

### 阶段 2 · `EditHistory` 这个类（还不接信号）

- [x] **2.1 `EditHistory`**（D2、D6、D7）
  - 文件：新增 `Tools/scene_editor/src/edit_history.h`、`edit_history.cpp`；
    加进 `Tools/scene_editor/CMakeLists.txt` 的源文件列表
  - 做法：`std::deque<Entry>` 两条（undo / redo），`Entry { std::string text; std::optional<UUID>
    selection; uint64_t state_id; }`，另有一份 `m_current`。API：
    ```cpp
    void reset(const engine::World&, const std::optional<core::UUID>& selection);
    void beginFrame(const std::optional<core::UUID>& selection);   // D6：记帧初选中
    void requestCommit();                                          // D3：显式请求
    bool shouldCommit(bool interaction_ended) const;
    bool commitIfChanged(const engine::World&, const std::optional<core::UUID>& selection);
    bool canUndo() const;  bool canRedo() const;
    bool undo(engine::World&, std::optional<core::UUID>& selection);
    bool redo(engine::World&, std::optional<core::UUID>& selection);
    uint64_t currentStateId() const;                               // D8
    ```
    上限按 D7 的两条一起裁。提交时清空 redo 栈。
  - **不引用 `EditorContext`** —— World 和选中都按参数传。这样它不知道编辑器的模式 / 面板，
    也就能被单独想清楚（`isSimulating()` 的判断留在调用方，见 3.1）。
  - 验收：编译过；此时没有任何调用方，编辑器行为不变。
  - **已完成，和计划有三处不同：**
    - 没有 `shouldCommit()`，那个门判并进了 `commitIfChanged(world, selection, interaction_ended)`
      —— 门的逻辑只该有一个地方。
    - `kMaxEntries` / `kMaxBytes` 放头文件当公开常量（上限是契约的一部分，测试也要用）。
    - 加了 `edit_history_test`（见交接区的偏差第 1 条），所以这一步不是「编译过就算」了。

- [x] **2.2 挂到 `EditorContext` 上**
  - 文件：`Tools/scene_editor/src/editor_context.h`、`editor_context.cpp`
  - 做法：`std::unique_ptr<EditHistory> m_history` + `EditHistory& history()`。头里只前向声明
    （`EditorContext` 的析构已经在 `.cpp` 里，不用额外改）。放这儿是因为面板和 layer 都拿得到
    `EditorContext`，不用新开一条 plumbing。
  - 验收：编译过；行为不变。
  - **已完成。**构造函数里先拿一个空场景的基线，这样「刚起编辑器就按 Ctrl+Z」是明确的空操作
    （不依赖 `SceneDocument` 有没有来得及给基线）。

- [x] **2.3 换场景时清历史**
  - 文件：`Tools/scene_editor/src/scene_document.cpp`
  - 做法：`reset()` 末尾 `history().reset(world, {})`；**并且**在 `createNew()`（`populateDefault()`
    之后）和 `load()`（成功和失败两条路都要）末尾再 `reset()` 一次。
  - **顺序是关键**：只在 `reset()` 里清的话，基线会是那个空场景，`populateDefault()` 摆进去的
    默认场景就成了「一次未提交的改动」，第一次 Ctrl+Z 会把整个默认场景抹掉。
  - 验收：见 3.4。
  - **已完成，和 4.1 一起做的**（同样那四处代码）。抽了一个 `resetHistoryBaseline()`：
    重取基线 + 把「存盘时是哪个状态」对齐到它，两件事永远一起发生。

### 阶段 3 · 接上信号（这一步之后真能用）

- [x] **3.1 帧初 / 帧末的钩子**（D3、D5）
  - 文件：`Tools/scene_editor/src/editor_layer.cpp` 的 `onImGuiRender()`
  - 做法：`m_imgui->beginFrame()` 之后、面板之前调 `beginFrame(选中)`；
    `m_imgui->endFrame()` **之前**（此时所有面板和 gizmo 都画完了）算下降沿并提交：
    ```cpp
    const bool gizmo_using = gizmo_enabled && m_gizmo->isUsing();
    const bool interaction_ended = (!ImGui::IsAnyItemActive() && m_was_item_active) ||
                                   (!gizmo_using && m_was_gizmo_using);
    if (!m_context->isSimulating() && history.shouldCommit(interaction_ended)) {
        history.commitIfChanged(m_context->world(), m_context->selectedEntity());
    }
    m_was_item_active = ImGui::IsAnyItemActive();
    m_was_gizmo_using = gizmo_using;
    ```
  - `gizmo_enabled &&` 那一下是必需的：Play 模式下 `EditorGizmo::draw()` 根本不被调用
    （`editor_layer.cpp:183-185` 提前 return），`m_using` 会停在上一次的值。
  - 验收：见 3.4。

- [x] **3.2 五个显式请求点**（D3）
  - 文件：`hierarchy_panel.cpp`（落地 duplicate / delete 之后各一次、两个 Create 菜单项各一次）、
    `editor_layer.cpp`（`spawnAssetEntity()` 两条成功路径）
  - 做法：在改完场景之后调一次 `requestCommit()`。**`Ctrl+D` / `Del` 的快捷键不用单独加** ——
    它们只是置 `m_pending_*`，真正的改动在 `HierarchyPanel::draw()` 里落地，那里已经有请求了。
  - 验收：见 3.4。
  - **已完成，六处。**`spawnAssetEntity()` 那两处原来是 `m_document->markDirty()`，**换成了**
    `requestCommit()` —— 脏标记现在跟着历史项走，再手动置一次反而会让「撤销回存盘状态」变不回
    干净（`markDirty()` 的语义是强制置脏）。

- [x] **3.3 菜单项 + 快捷键**
  - 文件：`Tools/scene_editor/src/editor_layer.cpp`
  - 做法：`canUndo()` / `canRedo()` 两个成员函数（**和菜单项、快捷键共用**，照
    `canSaveScene()` 的样子 —— `Applications.md` 那条「判断只写一处」）：判项目开着、
    `!isSimulating()`（D5）、`!IsAnyItemActive() && !gizmo->isUsing()`（D5 末段）、栈非空。
    那两个空菜单项接上；`handleShortcuts()` 里加 `Ctrl+Z` / `Ctrl+Y` 两条
    `ImGui::Shortcut(..., RouteGlobal)`，并把那条「刻意不接」的注释换掉。
  - 验收：见 3.4。
  - **已完成。**共用的前提抽成了 `canEditHistory()`，`canUndo()` / `canRedo()` 各自再加一条
    「栈非空」。`Del` 那条注释里「不可撤销」的说法也跟着改了 —— 但那道 `WantTextInput` 的防线
    **没拆**：能救回来仍然不如别误删。

- [x] **3.4 阶段 3 的手动验收（必须真的在编辑器里做完）**
  - **一次交互一条历史项**：拖 gizmo 移动一个立方体（拖 2 秒），按一次 `Ctrl+Z` 回到原位 ——
    **不是**按十几次才回去。Inspector 里拖 Intensity 同理。
  - **不产生空历史项**：点几下 UUID 那行（复制到剪贴板）、展开收起几个组件头、点来点去换选中 ——
    然后按 `Ctrl+Z`：**必须撤销的是上一次真的改动**，而不是「按了没反应」。这条直接验 D2。
  - **建 / 删 / 复制 / 拖资产**四种都能撤销，且 `Ctrl+Y` 都能重做回来。
  - **选中一起恢复**：选中 E → 删掉它 → `Ctrl+Z` → E 回来**并且是选中状态**（验 D6）。
  - **模拟期间**：进 Simulate，菜单里 Undo / Redo 是灰的、`Ctrl+Z` 没反应；Stop 之后历史还在，
    而且能撤销到进 Simulate 之前的编辑（验 D5）。
  - **换场景清历史**：New Scene 之后 Undo 是灰的（第一次 `Ctrl+Z` **不会**把默认场景抹掉，验 2.3）。
  - **拖拽中按 Ctrl+Z**：按着拖动框不放的同时按 `Ctrl+Z`，不许崩、不许出现半个场景（验 D5 末段）。

  **其中这三条已经由 `edit_history_test` 自动覆盖了逻辑那一半**，人工要确认的是「信号真的按预期
  到达」而不是逻辑对不对：一次交互一条历史项（测试里模拟了 5 帧拖拽 + 松手）、不产生空历史项、
  选中随删除一起恢复。剩下四条（模拟中灰化、换场景清历史、拖资产、拖拽中按 Ctrl+Z）只有人工。

  - **已完成。用户 2026-09-04 在编辑器里七条全走过，没有问题。** 顺带把端到端验收第 7 条
    （提交耗时）也覆盖了：从松开鼠标到画面响应没有可感的卡顿 —— 一次拖拽的验收本来就在盯这个。

### 阶段 4 · 脏标记与那个预存在的 bug

- [x] **4.1 脏标记由历史推导**（D8）
  - 文件：`scene_document.h`、`scene_document.cpp`
  - 做法：`m_saved_state_id` 取代 `bool m_dirty`；`isDirty()` = `history.currentStateId() !=
    m_saved_state_id`；`write()` / `load()` / `reset()` 之后把它对齐到当前 id。`markDirty()`
    保留（D8）—— 它现在的语义变成「强制置脏」，把 `m_saved_state_id` 设成一个不可能的值。
  - 验收：改一个数 → 脏；`Ctrl+Z` 撤回到存盘时的状态 → **不脏**；再改 → 脏。
  - **已完成，和 2.3 一起做的。**和计划有一处不同：**没有用「临时打一行看完就删」那招**，
    而是在工具栏 `[Edit]` 后面加了一个琥珀色 `*`（带 tooltip）。理由见交接区偏差第 5 条。
    `write()` 只对齐编号、**不清历史** —— 存一次盘不该让之前的编辑撤不回来。
    「撤销回存盘状态就变干净」这条由 `edit_history_test` 里的状态编号断言覆盖。

- [x] **4.2 修 Environment 的 UUID 缓存**（D9、证据 6）
  - 文件：`Tools/scene_editor/src/panels/inspector_panel.cpp`
  - 做法：`environmentEditorStates()` 的值从 `std::string` 换成带 `applied` 的结构，
    同步条件照 MeshRenderer：组件的值和上次写进去的不一致就重新同步文本。
  - 验收：选中 Environment 实体，改 Equirect Texture 的 UUID → `Ctrl+Z` → 输入框和组件
    **都**回到旧值，且**下一帧不会被写回去**（改完多等几秒再看，别看一眼就走 —— 这个 bug 的
    症状正是「撤销那一瞬间是对的，下一帧被缓存写回去了」）。修之前这条是红的。
  - **已完成。用户 2026-09-04 在编辑器里验过，没有问题。**
  - 比计划多了一个 `initialized` 位：不能光比较 `applied`，它的初值是 `UUID{0}`，而「没设贴图」
    的组件里存的正好也是 0，第一帧不会同步，输入框会是空的而不是一串 0（那是行为变化，
    不是这个 bug 要修的东西）。
  - **3.4 那七条覆盖不到这一条**：它只在「改了 Equirect Texture 这个字段」时才发作，
    而且只有那个 Environment 实体正选中着的时候。所以它值得单独一条验收。

### 阶段 5 · 文档

- [x] **5.1 架构文档**
  - `docs/Architecture/Applications.md` —— `EditorLayer` 结构图加 `EditHistory`、快捷键表加
    `Ctrl+Z` / `Ctrl+Y`、新增「撤销 / 重做」一节（快照 vs 命令、文本 vs 克隆、提交时机那张表、
    两条限制、选中语义、脏标记、以及「只有序列化过的组件能撤销」那条边界）、把那段「刻意没接」
    改写成「Del 现在能撤销了，但那道防线别拆」
  - `docs/Architecture/README.md` —— 缺口表加一行「撤销的粒度和覆盖面」（三条边界）、
    §6 的测试那一条从「只有 asset_pipeline_smoke」更新成现在的 10 条并点明「渲染画面没有自动化覆盖」
  - `docs/Architecture/Scene.md` —— `World` 的 API 列表加 `captureScene` / `restoreScene` 和它们
    和 `loadScene` 的两处刻意不同；「加一个组件要动哪几处」末尾补上「只注册拷贝不注册序列化的
    组件撤不回来」
  - 验收：一个没参与这次改动的人能只读文档说出「为什么在 42 个控件上一个钩子都不用埋」。

- [x] **5.2 任务收尾**
  - 头部状态改「已完成」，交接区留结论 + 三条「留给下一个人」的注意。

---

## 端到端验收

1. `cmake --preset debug` + `cmake --build --preset debug` 干净通过，无新增 warning。**已过。**
2. `ctest` **10 条**全绿（原 8 条 + `scene_snapshot_smoke` + `edit_history_test`）。**已过。**
3. `scene_snapshot_smoke` 的 `canonicalFormHolds` **反向验证过会失败**（去掉序列化里按 UUID 的
   排序）。注意是这一条 —— 「连续两次 capture 相同」那条反向验证**不会**红，理由见交接区。**已过。**
4. `edit_history_test` 的「空提交不进栈」**反向验证过会失败**（去掉 `commitIfChanged` 里的文本
   比较）。这是 D2 的核心。**已过。**
5. 阶段 3.4 那七条手动验收全过，其中这三条是这个任务真正的防线：
   **一次拖拽一条历史项**、**点了不改东西不产生历史项**、**删除撤销后选中回来**。
   **已过** —— 用户 2026-09-04 在编辑器里全走过。
6. 阶段 4.2 那条 Environment 的验收过（修之前是红的）。**已过** —— 用户 2026-09-04 验过。
7. **提交耗时是可接受的**：`projects/` 里那个场景上，从松开鼠标到画面响应没有可感的卡顿。
   **已过**（3.4 里那条「拖 2 秒再撤销」的验收本来就在盯这个）。真要量化得在
   `commitIfChanged` 里打一条耗时日志，那不在本任务范围内。
8. `arti_player` 不受影响（它不链编辑器代码，但 `World` 动了）：正常加载并渲染 `projects/` 的场景。
   **已过** —— `arti_player --project projects/projects.artiproj --stats` 起来、读进
   `1.artiscene`（10 个实体）、渲出第一帧 4 个 draw call，日志里唯一的 warning 是预先就有的
   AMD switchable graphics 那条。

编辑器本身起得来这一条**已经确认**：起动、自动恢复上次的项目和 `physics_test.artiscene`
（12 个实体）、编译着色器、渲出第一帧，日志里没有新的 error / warning。

**不在验收范围内**：内存占用的具体数字。D7 的两条上限是按量级定的，真要量就量「64 条满栈时
进程涨了多少」，而不是猜。

---

## 风险与注意

### 最大的风险是「撤销看起来生效了，其实有个字段没回来」

D2 的代价就在这儿：**只有序列化过的组件能撤销**。现在八个引擎组件都两边注册了，所以此刻是零风险；
但哪天有人加一个「注册了拷贝、没注册序列化」的运行时组件（`Scene.md:217` 说这是允许的），
它就会静默撤不回来。**5.1 必须把这条边界写进文档**，而且写在加组件的那张清单旁边
（`Scene.md` 第 6 节），不是只写在这份任务文件里 —— 任务文件没人会去翻。

证据 6 那个 bug 是同一类症状的另一个来源，而且它已经存在了：**「撤销对某个字段不生效」的
第一嫌疑人是面板的缓存，不是历史栈。** 4.2 修的就是这个。

### 别在 `EditHistory` 里判模式

`isSimulating()` 的判断留在 `EditorLayer`（3.1、3.3）。让 `EditHistory` 去问模式，就等于让它认识
`EditorContext`，那之后「历史栈为什么没记这一条」会变成要同时读两个类才能回答的问题。

### 改 `CMakeLists.txt` 会弄坏 cmake 缓存

1.3 和 2.1 都改。见交接区那条 PATH 说明，**先补 PATH 再改**。

### `.clang-format` 和实际风格不符

别用 clang-format 格式化整个文件 —— 它会把函数大括号改成另一种风格，制造一堆无关 diff。
照周围代码手写：4 空格、100 列、LF。

### 工作区里那份未提交的场景改动

`projects/Assets/Scenes/physics_test.artiscene` 开工前就有一份未提交的改动（UUID 被换成了
`000000000000a001` 这种手写的稳定值）。**不要 `git add .`**，提交时逐个点文件。
