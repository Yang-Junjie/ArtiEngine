#include "asset_tools/asset_pipeline.h"

#include "asset/builtin_assets.h"
#include "asset/importers/gltf_importer.h"
#include "asset/importers/obj_importer.h"
#include "asset/importers/texture_importer.h"
#include "asset/loaders/material_loader.h"
#include "asset/loaders/mesh_loader.h"
#include "asset/loaders/prefab_loader.h"
#include "asset/loaders/texture_loader.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <utility>

namespace arti::tools::asset {
namespace {

std::string normalizedExtension(std::string extension) {
    if (!extension.empty() && extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }
    std::ranges::transform(extension, extension.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

bool belongsToSource(const std::filesystem::path& candidate, const std::filesystem::path& source) {
    const std::string candidate_text = candidate.lexically_normal().generic_string();
    const std::string source_text = source.lexically_normal().generic_string();
    return candidate_text == source_text ||
           (candidate_text.size() > source_text.size() && candidate_text.starts_with(source_text) &&
                   candidate_text[source_text.size()] == '.');
}

size_t typeRank(std::string_view type, std::span<const std::string_view> preferred_types) {
    const auto found = std::ranges::find(preferred_types, type);
    return found == preferred_types.end()
                   ? preferred_types.size()
                   : static_cast<size_t>(std::distance(preferred_types.begin(), found));
}

} // namespace

AssetPipeline::~AssetPipeline() { close(); }

bool AssetPipeline::open(const std::filesystem::path& assets_root,
        const std::filesystem::path& artifacts_root) {
    close();

    std::error_code error;
    const auto normalized_assets = std::filesystem::absolute(assets_root, error).lexically_normal();
    if (error) {
        return false;
    }

    m_manager = std::make_unique<arti::asset::AssetManager>();
    if (!m_manager->open(normalized_assets, artifacts_root)) {
        close();
        return false;
    }

    const bool loaders_registered =
            m_manager->registerLoader(std::make_unique<engine::asset::MeshLoader>()) &&
            m_manager->registerLoader(std::make_unique<engine::asset::MaterialLoader>()) &&
            m_manager->registerLoader(std::make_unique<engine::asset::TextureLoader>()) &&
            m_manager->registerLoader(std::make_unique<engine::asset::PrefabLoader>());
    const bool importers_registered =
            registerImporter(std::make_unique<engine::asset::ObjImporter>()) &&
            registerImporter(std::make_unique<engine::asset::GltfImporter>()) &&
            registerImporter(std::make_unique<engine::asset::TextureImporter>());
    if (!loaders_registered || !importers_registered ||
            !engine::asset::ensureBuiltinAssets(*m_manager)) {
        close();
        return false;
    }

    m_assets_root = normalized_assets;
    refreshMetadata();
    return true;
}

void AssetPipeline::close() noexcept {
    if (m_manager) {
        m_manager->close();
    }
    m_manager.reset();
    m_importers.clear();
    m_assets_root.clear();
    m_metadata.clear();
    m_source_cache.clear();
}

bool AssetPipeline::isOpen() const noexcept {
    return m_manager != nullptr && m_manager->storage().isOpen();
}

bool AssetPipeline::registerImporter(std::unique_ptr<arti::asset::AssetImporter> importer) {
    if (!importer || m_manager == nullptr) {
        return false;
    }

    std::vector<std::string> extensions;
    try {
        extensions = importer->getSupportedExtensions();
    } catch (...) {
        return false;
    }

    auto* registered = importer.get();
    if (!m_manager->registerImporter(std::move(importer))) {
        return false;
    }
    for (std::string extension: extensions) {
        extension = normalizedExtension(std::move(extension));
        if (extension.empty() || !m_importers.emplace(std::move(extension), registered).second) {
            return false;
        }
    }
    return true;
}

bool AssetPipeline::canImport(const std::filesystem::path& relative_path) const {
    if (!arti::asset::isSafeAssetRelativePath(relative_path)) {
        return false;
    }
    return m_importers.contains(normalizedExtension(relative_path.extension().string()));
}

AssetImportSummary AssetPipeline::importFile(const std::filesystem::path& relative_path) {
    return importFile(relative_path, true);
}

AssetImportSummary AssetPipeline::importFile(const std::filesystem::path& relative_path,
        bool refresh_metadata) {
    AssetImportSummary summary;
    if (!isOpen()) {
        summary.error = "the asset workspace is not open";
        return summary;
    }
    if (!arti::asset::isSafeAssetRelativePath(relative_path)) {
        summary.error = "the source path is not a safe Assets-relative path";
        return summary;
    }

    const auto importer = m_importers.find(normalizedExtension(relative_path.extension().string()));
    if (importer == m_importers.end()) {
        summary.status = AssetImportStatus::Unsupported;
        summary.error = "no importer supports this source extension";
        return summary;
    }

    const auto results = m_manager->import(relative_path.lexically_normal(), *importer->second);
    if (results.empty()) {
        summary.error = "the asset manager did not produce an import result";
        return summary;
    }
    for (const auto& result: results) {
        if (!result) {
            summary.error = result.error;
            return summary;
        }
        summary.output_count += result.outputs.size();
    }

    summary.status = AssetImportStatus::Imported;
    if (refresh_metadata) {
        refreshMetadata();
    }
    return summary;
}

AssetScanSummary AssetPipeline::importPending() {
    AssetScanSummary summary;
    if (!isOpen()) {
        summary.traversal_error = "the asset workspace is not open";
        return summary;
    }

    bool metadata_changed = false;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator{ m_assets_root,
        std::filesystem::directory_options::skip_permission_denied, error };
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file(error)) {
            const auto& path = iterator->path();
            if (normalizedExtension(path.extension().string()) !=
                    arti::asset::kAssetMetadataExtension) {
                ++summary.source_files;
                const auto relative = std::filesystem::relative(path, m_assets_root, error);
                if (!error) {
                    if (!canImport(relative)) {
                        ++summary.unsupported_files;
                    } else if (isImported(relative)) {
                        ++summary.current_files;
                    } else {
                        const auto imported = importFile(relative, false);
                        if (imported.succeeded()) {
                            ++summary.imported_files;
                            metadata_changed = true;
                        } else {
                            ++summary.failed_files;
                        }
                    }
                }
            }
        }
        iterator.increment(error);
    }
    if (error) {
        summary.traversal_error = error.message();
    }
    if (metadata_changed) {
        refreshMetadata();
    }
    return summary;
}

const std::vector<arti::asset::AssetMetadata>& AssetPipeline::sourceAssets(
        const std::filesystem::path& relative_path) const {
    static const std::vector<arti::asset::AssetMetadata> empty;
    if (!arti::asset::isSafeAssetRelativePath(relative_path)) {
        return empty;
    }

    const auto normalized = relative_path.lexically_normal();
    const std::string key = normalized.generic_string();
    if (const auto found = m_source_cache.find(key); found != m_source_cache.end()) {
        return found->second;
    }

    std::vector<arti::asset::AssetMetadata> matches;
    for (const auto& entry: m_metadata) {
        if (belongsToSource(entry.source_path, normalized)) {
            matches.push_back(entry);
        }
    }
    return m_source_cache.emplace(key, std::move(matches)).first->second;
}

bool AssetPipeline::isImported(const std::filesystem::path& relative_path) const {
    return !sourceAssets(relative_path).empty();
}

std::vector<arti::asset::AssetMetadata> AssetPipeline::findAssetsBySource(
        const std::filesystem::path& relative_path) const {
    return sourceAssets(relative_path);
}

std::optional<arti::asset::AssetMetadata> AssetPipeline::primaryAsset(
        const std::filesystem::path& relative_path,
        std::span<const std::string_view> preferred_types) const {
    const auto& matches = sourceAssets(relative_path);
    if (matches.empty()) {
        return std::nullopt;
    }

    const auto normalized = relative_path.lexically_normal();
    const auto best = std::ranges::min_element(matches,
            [&normalized, preferred_types](const auto& left, const auto& right) {
                const auto key = [&normalized, preferred_types](const auto& entry) {
                    return std::tuple{ typeRank(entry.type, preferred_types),
                        entry.source_path.lexically_normal() == normalized ? 0 : 1,
                        entry.source_path.generic_string(), entry.type, entry.handle.value() };
                };
                return key(left) < key(right);
            });
    return *best;
}

AssetValidationSummary AssetPipeline::validate() const {
    AssetValidationSummary summary;
    summary.assets_checked = m_metadata.size();
    if (!isOpen()) {
        summary.issues.push_back({ {}, "the asset workspace is not open" });
        return summary;
    }

    for (const auto& entry: m_metadata) {
        std::error_code error;
        const auto artifact = m_manager->storage().resolveArtifactPath(entry.artifact_path);
        if (!artifact || !std::filesystem::is_regular_file(*artifact, error) || error) {
            summary.issues.push_back(
                    { entry.handle, "missing artifact: " + entry.artifact_path.generic_string() });
        }
        for (const core::UUID dependency: entry.dependencies) {
            if (!m_manager->catalog().find(dependency)) {
                summary.issues.push_back(
                        { entry.handle, "missing dependency: " + dependency.toString() });
            }
        }
    }
    return summary;
}

arti::asset::AssetManager& AssetPipeline::manager() {
    if (!m_manager) {
        throw std::logic_error("AssetPipeline is not open");
    }
    return *m_manager;
}

const arti::asset::AssetManager& AssetPipeline::manager() const {
    if (!m_manager) {
        throw std::logic_error("AssetPipeline is not open");
    }
    return *m_manager;
}

void AssetPipeline::refreshMetadata() {
    m_metadata = m_manager->catalog().allMetadata();
    std::ranges::sort(m_metadata, [](const auto& left, const auto& right) {
        return std::tuple{ left.source_path.generic_string(), left.type, left.handle.value() } <
               std::tuple{ right.source_path.generic_string(), right.type, right.handle.value() };
    });
    m_source_cache.clear();
}

} // namespace arti::tools::asset
