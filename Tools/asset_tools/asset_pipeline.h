#pragma once

#include "artichoco/asset/asset_manager.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace arti::tools::asset {

enum class AssetImportStatus : uint8_t { Imported, Unsupported, Failed };

struct AssetImportSummary {
    AssetImportStatus status{ AssetImportStatus::Failed };
    size_t output_count{ 0 };
    std::string error;

    bool succeeded() const noexcept { return status == AssetImportStatus::Imported; }
};

// 一个源文件在 Content Browser 里的状态。
enum class SourceState : uint8_t {
    Imported,     // 已导入，assets 非空
    Pending,      // 可导入但还没导
    Unsupported,  // 没有 importer 认领
    Stale         // 已登记但 artifact 缺失，需要重导
};

struct SourceAssets {
    SourceState state{ SourceState::Unsupported };
    std::vector<arti::asset::AssetMetadata> assets;
};

// 一个源文件的导入设置，供 Inspector 编辑。
struct SourceSettings {
    std::vector<arti::asset::SettingDescriptor> schema;
    // 解析后的有效值，以及每个键来自哪一层。
    arti::asset::ResolvedSettings resolved;
    // 磁盘上原样存着的 Authored/Inferred。写回时只改 authored。
    arti::asset::AssetSettings stored;
    bool valid{ false };
};

// CPU-side asset workspace shared by editor frontends and command-line tools.
// Renderer-owned GPU caches deliberately remain outside this class.
class AssetPipeline {
public:
    AssetPipeline() = default;
    ~AssetPipeline();

    AssetPipeline(const AssetPipeline&) = delete;
    AssetPipeline& operator=(const AssetPipeline&) = delete;

    bool open(const std::filesystem::path& assets_root,
            const std::filesystem::path& artifacts_root);
    void close() noexcept;
    bool isOpen() const noexcept;

    bool canImport(const std::filesystem::path& relative_path) const;
    AssetImportSummary importFile(const std::filesystem::path& relative_path);

    // 三方对账。plan 只读，可以直接当 UI 视图；reconcile 是 plan + apply。
    arti::asset::ReconcilePlan planReconcile() const;
    arti::asset::ReconcileReport reconcile();

    arti::asset::AssetIntegrityReport checkIntegrity() const;

    // 某个源文件（含其子资产）的状态。分组表按 catalog revision 缓存，但这里
    // 按值返回：调用方常常在拿到结果之后又调 importFile()，那会撞 revision 并
    // 让缓存重建，引用会失效。
    SourceAssets sourceAssets(const std::filesystem::path& relative_path) const;
    bool isImported(const std::filesystem::path& relative_path) const;

    // 某个源文件的导入设置。schema 为空表示这个 importer 没有可调设置。
    SourceSettings sourceSettings(const std::filesystem::path& relative_path) const;
    // 写入一个 Authored 设置并立刻重导入该源文件。
    // value 为 nullopt 表示"清除用户设定"，回落到 inferred / default。
    bool setAuthoredSetting(const std::filesystem::path& relative_path, const std::string& key,
            const std::optional<arti::asset::Value>& value);

    struct ExtractResult {
        bool succeeded{ false };
        std::filesystem::path source_path;  // 新建的 .artimaterial（Assets-relative）
        core::UUID handle;                  // 提取物的 handle（导入后）
        std::string error;
    };

    // 把容器产出的派生材质提取成独立的 .artimaterial 源文件，并在容器的
    // sidecar 里记下覆盖，使 prefab 在重导入之后仍指向提取物。
    //
    // 这是"派生资产只读"的必然出口：用户想改 glTF 带来的材质，就把它变成
    // 自己拥有的 Root 资产。destination 为空时自动放在 Materials/ 下。
    ExtractResult extractMaterial(core::UUID material,
            const std::filesystem::path& destination = {});

    // 引擎自带资产，不属于任何用户源文件。
    std::vector<arti::asset::AssetEntry> engineAssets() const;

    std::vector<arti::asset::AssetMetadata> allMetadata() const;

    arti::asset::AssetManager& manager();
    const arti::asset::AssetManager& manager() const;

private:
    void invalidateCacheIfStale() const;

    std::unique_ptr<arti::asset::AssetManager> m_manager;
    std::filesystem::path m_assets_root;

    // source_path → 归属它的资产。整表按 catalog revision 重建，查询 O(1)。
    mutable std::unordered_map<std::string, SourceAssets> m_by_source;
    mutable uint64_t m_cached_revision{ 0 };
    mutable bool m_cache_valid{ false };
};

} // namespace arti::tools::asset
