# 资产

> 本页讲 **ArtiEngine 在资产框架之上填了什么**：有哪些资产类型、artifact 长什么样、
> 谁在编辑期跑、谁在运行期跑。框架本身（reconcile、sidecar v2、设置三层解析、prescan、
> 拓扑序、Extract）在 `ArtiRenderer/ArtiChoco/artichoco/asset/README.md`。

## 1. 两阶段

```
Import   外部源文件  →  引擎 artifact + .meta        编辑期，只有工具跑
Load     artifact    →  Asset 实例（CPU 侧）          运行期，播放器也跑
Upload   Asset       →  MeshHandle / TextureHandle    运行期，谁有 Renderer 谁跑
```

三条不变式（框架保证，不靠调用方自觉）：

```
Assets/ 源文件 + .meta      =  唯一真相
Library/ artifact           =  完全可推导，可随时整个删掉
catalog 里的 User 集合      =  磁盘 .meta 的纯函数
```

删掉 `Library/` 再 reconcile，一切恢复且 **UUID 不变**（身份存在 `.meta` 里）。删掉某个
`.meta`，那个资产会被重新导入并拿到**新 UUID**，场景引用会断。

## 2. 五种资产类型

`ArtiEngine/asset/`。每种都继承 `arti::asset::Asset`，有一个稳定的类型串（进 `.meta`，
改名就是改文件格式）。

| 类型 | 类型串 | artifact | 格式 | 内容 |
| --- | --- | --- | --- | --- |
| 网格 | `artiengine.asset.mesh` | `.artimesh` | 二进制 `MSHA` v1 | 顶点 / 索引 / submesh / **材质槽名字** / AABB |
| 纹理 | `artiengine.asset.texture` | `.artitexture` | 二进制 `TEXA` | 已解码的 texel、宽高、`TextureFormat`、要不要生成 mip |
| 材质 | `artiengine.asset.material` | `.artimaterial` | YAML | PBR 参数 + 五张贴图的 handle |
| Prefab | `artiengine.asset.prefab` | `.artiprefab` | YAML | 节点树，每个节点记 local transform、parent、mesh UUID、materials UUID |
| 脚本 | `artiengine.asset.script` | `.artiscript` | UTF-8 文本 | 一段 Lua 源码，和源文件**逐字节相同** |

二进制的两种（网格、纹理）都是体量大、结构固定的；YAML 的两种（材质、prefab）小、需要
人能读能 diff。网格 artifact 里有 `static_assert(sizeof(MeshVertex) == 56)` —— 顶点布局
一变，编码就必须跟着改，而不是静默写出一份格式不对的文件。

### 引用方向（容易搞反）

```
MeshAsset      只有 material_slots 名字，从不绑具体材质
PrefabNode     绑 mesh UUID + materials UUID 列表   ← 绑定发生在这里
MaterialAsset  引用 texture handle
```

所以拖一个 **prefab** 进 Viewport 会按节点树生成实体，材质是作者指定的；拖一个 **mesh**
只生成单个实体，材质用 builtin default。

`MaterialAsset` 额外持住 `AssetManager` 已经解析好的贴图 `shared_ptr`：
`AssetManager::m_loaded` 是 `weak_ptr`，而纹理跨源共享，材质必须自己保证被引用的纹理不被
回收 —— 否则它手里的 UUID 会指向一个已经消失的资产。

## 3. 三个 importer / 四个 loader

| importer | 认领 | 产出 |
| --- | --- | --- |
| `artiengine.GltfImporter` | `.gltf` `.glb` | 材质 / 网格 / prefab 子资产；外部贴图**不重复解码**，查已导入的纹理 handle |
| `artiengine.TextureImporter` | `.png .jpg .jpeg .bmp .tga .gif .hdr` | 一张纹理（`Colorspace` 设置决定 sRGB / Unorm） |
| `artiengine.MaterialImporter` | `.artimaterial` | 一个材质（`.artimaterial` 是**真实源文件**，不是特例） |
| `artiengine.ScriptImporter` | `.lua` | 一份脚本。没有设置、没有子资产、没有跨源引用 —— artifact 就是源文件的拷贝 |

loader 五个，和五种资产一一对应，都是无状态的纯解码器：`MeshLoader` / `TextureLoader` /
`MaterialLoader` / `PrefabLoader` / `ScriptLoader`。

**脚本为什么也走这条管线**（而不是像 `.artiscene` 那样按项目根相对路径引用）：走了它就自动
拿到 UUID 身份、`.meta`、reconcile、以及**跟着 `pack` 走**（`pack` 整树拷 `Library/Artifacts/`，
但 `Assets/` 下只拷 `.artiscene`）。发布出去的游戏因此只读 artifact，不需要 `.lua` 源文件在场。

`.artimaterial` 既是源文件又是 artifact 扩展名，这不是巧合：编辑器创作的材质就是普通的
Root 资产，管线因此不需要「无源文件的用户资产」这种特例。源文件里贴图用**路径**引用
（人可读、可 diff），导入时解析成 UUID。

## 4. AssetRuntime 与 AssetPipeline

同一个 `AssetManager`，两层包装，分界线是「运行时需不需要」。

```
tools::asset::AssetPipeline          编辑期。importer + reconcile + 按源文件分组 + 设置编辑
        ▲                                        + Extract + 打包
        │ 建立在
engine::AssetRuntime                运行期。AssetManager + 四个 loader + builtin
```

`AssetRuntime`（`ArtiEngine/runtime/asset_runtime.h`）：

- **只注册 loader，不注册 importer**。一个已经导好的项目跑起来不需要 cgltf 和 stb_image，
  运行时也不该背上它们。
- 两条打开方式：
  - `open(assets_root, artifacts_root)` —— **开发模式**，catalog 从 `Assets/` 下的 `.meta`
    扫出来，源文件树必须在。
  - `openPackaged(artifacts_root, manifest_file)` —— **打包模式**，catalog 从 manifest 建，
    只读 artifact，`Assets/` 不需要存在。发布出去的游戏走这条。
- 失败时保持关闭状态，不留半开的工作区。未打开时取 `manager()` 抛 `std::logic_error`
  —— 比让调用方拿一个空引用再解引用更早也更明确。
- 每次 `open` 重建 `AssetManager` 而不是复用：`AssetManager::close()` 只清 storage /
  catalog / 缓存，不清 loader、importer 和 engine provider，复用会让它们越攒越多。

**loader 的注册全工程只有这一处**。这是为了让「加了第五种资产类型」不可能只在一边生效 ——
不会出现编辑器认得、player 不认得的资产。

`AssetPipeline`（`Tools/asset_tools/asset_pipeline.h`）在它之上加编辑期的东西：

| 能力 | 说明 |
| --- | --- |
| `planReconcile()` / `reconcile()` | plan 是纯读的，所以同时是 dry-run 报告、Content Browser 的视图数据、和确定性的执行顺序 |
| `sourceAssets(path)` | 一个源文件的状态（`Imported` / `Pending` / `Unsupported` / `Stale`）和它产出的资产。整表按 catalog `revision()` 缓存，查询 O(1)，**按值返回** —— 调用方常常拿到结果之后又 `importFile()`，那会撞 revision 让缓存重建 |
| `sourceSettings` / `setAuthoredSetting` | 读 / 写导入设置。写只动 `Authored` 一层并立刻重导；传 `nullopt` 表示「清除用户设定」，回落到 inferred / default。**目前只有 CLI 在调**，编辑器还没有对应 UI |
| `extractMaterial` | 把容器产出的只读派生材质提取成独立的 `.artimaterial` Root 资产，并在容器 sidecar 里记下覆盖，使 prefab 重导后仍指向提取物 |
| `checkIntegrity()` | 只读校验，打包前的门槛 |

## 5. GPUAssetCache

`ArtiEngine/asset/gpu_asset_cache.h`。按 UUID 缓存 `MeshHandle` / `MaterialHandle` /
`TextureHandle`，需要时才 `load` + 上传。

刻意**不在** `AssetRuntime` 里：`asset_tools` 是无窗口 CLI，它消费 `AssetPipeline` 也就间接
消费了 `AssetRuntime`，但它没有 `Renderer`。**谁有 Renderer 谁持 GPU 缓存**：

- 编辑器 → `EditorProject` 持有（和 `AssetPipeline` 一起，生命周期都绑在项目上 ——
  换项目要重新扫 `.meta`、重新上传）
- 播放器 → `PlayerLayer` 持有
- CLI → 没有

内部还有一张 `m_failed` 表：上传失败的 UUID 记下来，避免每帧重试同一个坏资产。

## 6. builtin 资产

`ArtiEngine/asset/builtin_assets.h`。三个编译期常量 UUID：

```cpp
kBuiltinCubeMesh          0xB0117E1000000001
kBuiltinDefaultMaterial   0xB0117E1000000002
kBuiltinSphereMesh        0xB0117E1000000003
```

它们在 `Assets/` 下**既没有源文件也没有 `.meta`**（catalog 里 origin 是 `Engine` 而不是
`User`），几何和材质由代码生成，artifact 写在 `Library/Artifacts/Builtin/`。

`ensureBuiltinAssets()` 除了登记和补齐 artifact，还把自己注册成 `AssetManager` 的
engine asset provider，这样**每轮 reconcile 都能自愈被删掉的 builtin artifact** ——
三方对账管不到没有源文件的资产，只能靠 provider 回调。

## 7. 打包

`Tools/asset_tools/asset_packer.h`：

```
asset_tools pack <project> <out> [--overwrite] [--no-reconcile] [--no-runtime] [--no-player]
```

产物布局见 [README.md](README.md#项目打包后)。几条设计决定：

- **默认拒绝往非空目录里写**。往一份旧产物上盖会留下上一次的资产，而残留在游戏里的表现是
  「删掉的东西还在」—— 那种 bug 没人会怀疑到打包这一步。
- **先 `checkIntegrity()`，缺 artifact 就整体失败**。打出一份少东西的包，比打包失败糟得多。
- **默认先跑一遍 reconcile**，保证 artifact 是最新的。`--no-reconcile` 适合 CI 里 scan 和
  pack 分两步跑。
- **不含源模型、贴图，也不含任何 `.meta`**。运行时靠 `catalog.artimanifest` 建 catalog。
- **`.artiscene` 原位拷进 `Assets/`**。场景现在还不是资产（没有 handle、没有 artifact），
  而 `StartScene` 是项目根相对路径 —— 原位拷过去那条路径就仍然成立，不需要改写项目文件。
- **运行时文件从 `PackOptions::runtime_dir` 拷**，CLI 填的是 `core::executableDir()`，
  也就是 `asset_tools` 自己旁边 —— staging 已经把 `*.dll` 和 `shaders/` 放在那了。
  这样 pack 不需要知道 SDK 和源码树在哪，也自动跟着构建配置走。`--no-runtime` 只打资产。
- **缺 DLL 或缺 `shaders/` 算失败，缺 `arti_player` 只记 warning**。前两者产出的包一定跑不
  起来；播放器缺了补一个 exe 就能用，而 CI 里 pack 和 player 可能分开构建。
- **运行时文件放在最后一步拷**。前面任何一步失败都不会留下一个「有 exe 没资产」的半成品目录。

## 8. CLI

```
asset_tools <command> <project.artiproj> [args]

plan      三方对账的 dry-run 报告（不改磁盘）
scan      plan + apply
list      列出 catalog 里的资产
validate  完整性校验
import    导入单个源文件
settings  打印一个源文件的设置 schema 和逐键的有效值与来源
set       写一个 Authored 设置并立刻重导（值传 - 表示清除）
extract   把派生材质提取成独立 .artimaterial
pack      打包成可发布目录
```

`plan` 的退出码在有 UUID 冲突或坏 `.meta` 时是 1 —— 可以直接当 CI 的门槛。
