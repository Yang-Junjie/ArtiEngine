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

struct AssetScanSummary {
    size_t source_files{ 0 };
    size_t imported_files{ 0 };
    size_t current_files{ 0 };
    size_t unsupported_files{ 0 };
    size_t failed_files{ 0 };
    std::string traversal_error;

    bool succeeded() const noexcept { return traversal_error.empty() && failed_files == 0; }
};

struct AssetValidationIssue {
    core::UUID asset;
    std::string message;
};

struct AssetValidationSummary {
    size_t assets_checked{ 0 };
    std::vector<AssetValidationIssue> issues;

    bool succeeded() const noexcept { return issues.empty(); }
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
    AssetScanSummary importPending();

    bool isImported(const std::filesystem::path& relative_path) const;
    std::vector<arti::asset::AssetMetadata> findAssetsBySource(
            const std::filesystem::path& relative_path) const;
    std::optional<arti::asset::AssetMetadata> primaryAsset(
            const std::filesystem::path& relative_path,
            std::span<const std::string_view> preferred_types) const;

    std::span<const arti::asset::AssetMetadata> metadata() const noexcept { return m_metadata; }
    AssetValidationSummary validate() const;

    arti::asset::AssetManager& manager();
    const arti::asset::AssetManager& manager() const;

private:
    bool registerImporter(std::unique_ptr<arti::asset::AssetImporter> importer);
    AssetImportSummary importFile(const std::filesystem::path& relative_path,
            bool refresh_metadata);
    void refreshMetadata();
    const std::vector<arti::asset::AssetMetadata>& sourceAssets(
            const std::filesystem::path& relative_path) const;

    std::unique_ptr<arti::asset::AssetManager> m_manager;
    std::unordered_map<std::string, arti::asset::AssetImporter*> m_importers;
    std::filesystem::path m_assets_root;
    std::vector<arti::asset::AssetMetadata> m_metadata;
    mutable std::unordered_map<std::string, std::vector<arti::asset::AssetMetadata>> m_source_cache;
};

} // namespace arti::tools::asset
