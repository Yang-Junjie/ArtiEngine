#pragma once
#include <filesystem>
#include <memory>

namespace arti::asset {
class AssetManager;
} // namespace arti::asset

namespace arti::engine {

// CPU 侧的运行期资产工作区：AssetManager 加引擎的四个 loader 加 builtin 资产。
//
// 只注册 loader，不注册 importer —— 一个已经导好的项目跑起来不需要 cgltf 和 stb，运行时
// 也不该背上它们。importer 和 reconcile 在 tools::asset::AssetPipeline 里，它建立在这个类
// 之上，所以 loader 的注册全工程只有这一处：加了第五种资产类型不会只在一边生效。
//
// GPU 侧的 GPUAssetCache 刻意不在这里：asset_tools 是无窗口的命令行工具，它消费
// AssetPipeline 也就间接消费这个类，但它没有 Renderer。谁有 Renderer 谁持 GPU cache。
class AssetRuntime {
public:
    AssetRuntime();
    ~AssetRuntime();

    AssetRuntime(const AssetRuntime&) = delete;
    AssetRuntime& operator=(const AssetRuntime&) = delete;

    // 开发模式：catalog 从 Assets/ 下的 .meta 扫出来，源文件树必须在。
    // 失败时保持关闭状态，不留半开的工作区。
    bool open(const std::filesystem::path& assets_root,
            const std::filesystem::path& artifacts_root);

    // 打包模式：catalog 从 manifest 建，只读 artifacts，Assets/ 不需要存在。
    // 发布出去的游戏走这条 —— 见 arti::asset::kAssetManifestFileName。
    bool openPackaged(const std::filesystem::path& artifacts_root,
            const std::filesystem::path& manifest_file);

    void close() noexcept;
    bool isOpen() const noexcept;
    bool isPackaged() const noexcept;

    // 未打开时抛 std::logic_error —— 和调用方拿到一个空引用再解引用比，这个更早也更明确。
    arti::asset::AssetManager& manager();
    const arti::asset::AssetManager& manager() const;

private:
    // 两条 open 的公共收尾：注册 loader、补齐 builtin，成功了才把 manager 装进成员。
    bool finishOpen(std::unique_ptr<arti::asset::AssetManager> manager);

    // 每次 open 重建而不是复用：AssetManager::close() 只清 storage / catalog / 缓存，
    // 不清 loader、importer 和 engine provider，复用会让它们越攒越多。
    std::unique_ptr<arti::asset::AssetManager> m_manager;
};

} // namespace arti::engine
