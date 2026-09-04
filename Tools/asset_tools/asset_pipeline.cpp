#include "asset_tools/asset_pipeline.h"

#include "asset/importers/gltf_importer.h"
#include "asset/importers/material_importer.h"
#include "asset/importers/script_importer.h"
#include "asset/importers/texture_importer.h"

#include "asset/material_asset.h"

#include <algorithm>
#include <fstream>
#include <ranges>
#include <system_error>
#include <tuple>
#include <utility>

namespace arti::tools::asset {
AssetPipeline::~AssetPipeline() { close(); }

bool AssetPipeline::open(const std::filesystem::path& assets_root,
        const std::filesystem::path& artifacts_root) {
    close();

    std::error_code error;
    const auto normalized_assets = std::filesystem::absolute(assets_root, error).lexically_normal();
    if (error) {
        return false;
    }

    // loader 和 builtin 由 AssetRuntime 负责，这里只补上编辑期专属的 importer。
    if (!m_runtime.open(normalized_assets, artifacts_root)) {
        close();
        return false;
    }

    auto& runtime_manager = m_runtime.manager();
    const bool importers_registered =
            runtime_manager.registerImporter(std::make_unique<engine::asset::GltfImporter>()) &&
            runtime_manager.registerImporter(std::make_unique<engine::asset::TextureImporter>()) &&
            runtime_manager.registerImporter(std::make_unique<engine::asset::MaterialImporter>()) &&
            runtime_manager.registerImporter(std::make_unique<engine::asset::ScriptImporter>());
    if (!importers_registered) {
        close();
        return false;
    }

    m_assets_root = normalized_assets;
    return true;
}

void AssetPipeline::close() noexcept {
    m_runtime.close();
    m_assets_root.clear();
    m_by_source.clear();
    m_cache_valid = false;
    m_cached_revision = 0;
}

bool AssetPipeline::isOpen() const noexcept {
    return m_runtime.isOpen();
}

bool AssetPipeline::canImport(const std::filesystem::path& relative_path) const {
    return isOpen() && manager().canImport(relative_path);
}

AssetImportSummary AssetPipeline::importFile(const std::filesystem::path& relative_path) {
    AssetImportSummary summary;
    if (!isOpen()) {
        summary.error = "the asset workspace is not open";
        return summary;
    }
    if (!arti::asset::isSafeAssetRelativePath(relative_path)) {
        summary.error = "the source path is not a safe Assets-relative path";
        return summary;
    }
    if (!manager().canImport(relative_path)) {
        summary.status = AssetImportStatus::Unsupported;
        summary.error = "no importer supports this source extension";
        return summary;
    }

    const auto result = manager().import(relative_path.lexically_normal());
    if (!result) {
        summary.error = result.error;
        return summary;
    }
    summary.status = AssetImportStatus::Imported;
    summary.output_count = result.outputs.size();
    return summary;
}

arti::asset::ReconcilePlan AssetPipeline::planReconcile() const {
    if (!isOpen()) {
        arti::asset::ReconcilePlan plan;
        plan.traversal_error = "the asset workspace is not open";
        return plan;
    }
    return manager().planReconcile();
}

arti::asset::ReconcileReport AssetPipeline::reconcile() {
    if (!isOpen()) {
        arti::asset::ReconcileReport report;
        report.errors.emplace_back("the asset workspace is not open");
        return report;
    }
    return manager().reconcile();
}

arti::asset::AssetIntegrityReport AssetPipeline::checkIntegrity() const {
    if (!isOpen()) {
        arti::asset::AssetIntegrityReport report;
        report.issues.push_back({ {}, "the asset workspace is not open" });
        return report;
    }
    return manager().checkIntegrity();
}

// catalog 变了就整表重建一次：O(资产数)，而不是每次查询都全量线性扫描。
void AssetPipeline::invalidateCacheIfStale() const {
    if (!isOpen()) {
        m_by_source.clear();
        m_cache_valid = false;
        return;
    }
    const uint64_t revision = manager().catalog().revision();
    if (m_cache_valid && revision == m_cached_revision) {
        return;
    }

    m_by_source.clear();
    for (const arti::asset::AssetEntry& entry:
            manager().catalog().entriesWithOrigin(arti::asset::AssetOrigin::User)) {
        // 一源一 sidecar：source_path 就是拥有者，不需要回溯。
        m_by_source[entry.metadata.source_path.lexically_normal().generic_string()]
                .assets.push_back(entry.metadata);
    }

    for (auto& [source, bucket]: m_by_source) {
        std::ranges::sort(bucket.assets,
                [](const arti::asset::AssetMetadata& left,
                        const arti::asset::AssetMetadata& right) {
                    return std::tuple{ left.source_path.generic_string(), left.type,
                               left.handle.value() } <
                           std::tuple{ right.source_path.generic_string(), right.type,
                               right.handle.value() };
                });

        bucket.state = SourceState::Imported;
        for (const arti::asset::AssetMetadata& asset: bucket.assets) {
            if (!manager().storage().hasArtifact(asset.artifact_path)) {
                bucket.state = SourceState::Stale;
                break;
            }
        }
    }

    m_cached_revision = revision;
    m_cache_valid = true;
}

SourceAssets AssetPipeline::sourceAssets(const std::filesystem::path& relative_path) const {
    if (!isOpen() || !arti::asset::isSafeAssetRelativePath(relative_path)) {
        return {};
    }
    invalidateCacheIfStale();

    const std::string key = relative_path.lexically_normal().generic_string();
    if (const auto found = m_by_source.find(key); found != m_by_source.end()) {
        return found->second;
    }

    // 没有任何资产归属它：要么还没导，要么根本导不了。这种情况不进缓存 ——
    // 缓存只装"由 catalog 推导出来的"分组，未导入状态每次现算，成本是一次 map 查询。
    SourceAssets pending;
    pending.state = manager().canImport(relative_path) ? SourceState::Pending
                                                       : SourceState::Unsupported;
    return pending;
}

bool AssetPipeline::isImported(const std::filesystem::path& relative_path) const {
    return !sourceAssets(relative_path).assets.empty();
}

SourceSettings AssetPipeline::sourceSettings(const std::filesystem::path& relative_path) const {
    SourceSettings settings;
    if (!isOpen() || !arti::asset::isSafeAssetRelativePath(relative_path)) {
        return settings;
    }
    const auto* importer = manager().importerFor(relative_path);
    if (importer == nullptr) {
        return settings;
    }

    settings.schema = importer->getSettingSchema();
    if (const auto sidecar = manager().storage().readMetadata(relative_path)) {
        settings.stored = sidecar->settings;
    }
    settings.resolved = arti::asset::resolveSettings(settings.schema, settings.stored);
    settings.valid = true;
    return settings;
}

bool AssetPipeline::setAuthoredSetting(const std::filesystem::path& relative_path,
        const std::string& key, const std::optional<arti::asset::Value>& value) {
    if (!isOpen()) {
        return false;
    }
    const auto sidecar = manager().storage().readMetadata(relative_path);
    if (!sidecar) {
        return false;
    }

    arti::asset::SourceMetadata updated = *sidecar;
    if (value) {
        updated.settings.authored[key] = *value;
    } else {
        // 删除键而不是写入默认值 —— "键的存在"本身是信息，缺失表示
        // "未指定、往下层取"，写入默认值会永久压住推断。
        updated.settings.authored.erase(key);
    }
    if (!manager().storage().writeMetadata(updated)) {
        return false;
    }
    // 设置变了就重导入，artifact 才会按新设置重新编码。
    return importFile(relative_path).succeeded();
}

AssetPipeline::ExtractResult AssetPipeline::extractMaterial(core::UUID material,
        const std::filesystem::path& destination) {
    ExtractResult result;
    if (!isOpen()) {
        result.error = "the asset workspace is not open";
        return result;
    }

    const auto metadata = manager().catalog().find(material);
    if (!metadata) {
        result.error = "no such asset in the catalog";
        return result;
    }
    if (metadata->type != engine::asset::kMaterialAssetType) {
        result.error = "only materials can be extracted";
        return result;
    }
    if (metadata->local_id.empty()) {
        // local_id 为空说明它本来就是独立 Root 资产（比如已经是 .artimaterial）。
        result.error = "this material is already a standalone source asset";
        return result;
    }

    // 提取物要复制派生材质当前的参数，所以得先把它加载出来。
    const auto loaded = manager().load<engine::asset::MaterialAsset>(material);
    if (!loaded) {
        result.error = "failed to load the material";
        return result;
    }

    // artifact 里存的是纹理 UUID，源文件用路径引用（人可读、可 diff），
    // 所以要反查回去。查不到的槽位留空。
    const auto referenceFor = [this](core::UUID texture) -> std::string {
        if (!texture.isValid()) {
            return {};
        }
        const auto found = manager().catalog().find(texture);
        if (!found) {
            return {};
        }
        std::string text = found->source_path.generic_string();
        if (!found->local_id.empty()) {
            text += "#" + found->local_id;
        }
        return text;
    };

    const auto& params = loaded->params();
    engine::asset::MaterialSourceTextures textures;
    textures.base_color = referenceFor(params.base_color_texture.id());
    textures.metallic_roughness = referenceFor(params.metallic_roughness_texture.id());
    textures.normal = referenceFor(params.normal_texture.id());
    textures.occlusion = referenceFor(params.occlusion_texture.id());
    textures.emissive = referenceFor(params.emissive_texture.id());

    std::filesystem::path target = destination;
    if (target.empty()) {
        // 默认落在 Materials/ 下，用容器名 + local_id 避免撞名。
        target = std::filesystem::path{ "Materials" } /
                 (metadata->source_path.stem().string() + "_" + metadata->local_id +
                         ".artimaterial");
    }
    if (!arti::asset::isSafeAssetRelativePath(target)) {
        result.error = "the destination is not a safe Assets-relative path";
        return result;
    }
    if (manager().storage().hasSource(target)) {
        result.error = "the destination already exists: " + target.generic_string();
        return result;
    }

    const auto absolute = manager().storage().resolveSourcePath(target);
    if (!absolute) {
        result.error = "failed to resolve the destination path";
        return result;
    }
    std::error_code error;
    std::filesystem::create_directories(absolute->parent_path(), error);
    if (error) {
        result.error = "failed to create the destination directory: " + error.message();
        return result;
    }
    {
        std::ofstream output{ *absolute, std::ios::binary | std::ios::trunc };
        output << engine::asset::writeMaterialSource(params, textures);
        if (!output.good()) {
            result.error = "failed to write " + target.generic_string();
            return result;
        }
    }

    // 在容器的 sidecar 里记下覆盖，这样重导入之后 prefab 仍然指向提取物。
    auto sidecar = manager().storage().readMetadata(metadata->source_path);
    if (!sidecar) {
        result.error = "failed to read the container sidecar";
        return result;
    }
    sidecar->settings.authored[engine::asset::GltfImporter::kExtractedMaterialPrefix +
            metadata->local_id] = target.generic_string();
    if (!manager().storage().writeMetadata(*sidecar)) {
        result.error = "failed to record the extraction in the container sidecar";
        return result;
    }

    // 提取物先导入，容器才能引用它的 handle；然后重导容器让 prefab 改指向。
    if (!importFile(target).succeeded()) {
        result.error = "failed to import the extracted material";
        return result;
    }
    if (!importFile(metadata->source_path).succeeded()) {
        result.error = "failed to reimport the container";
        return result;
    }

    const auto extracted = manager().catalog().findBySourceAndLocalId(target, std::string{});
    if (!extracted) {
        result.error = "the extracted material did not register in the catalog";
        return result;
    }

    result.succeeded = true;
    result.source_path = target;
    result.handle = extracted->handle;
    return result;
}

std::vector<arti::asset::AssetEntry> AssetPipeline::engineAssets() const {
    if (!isOpen()) {
        return {};
    }
    auto entries = manager().catalog().entriesWithOrigin(arti::asset::AssetOrigin::Engine);
    std::ranges::sort(entries,
            [](const arti::asset::AssetEntry& left, const arti::asset::AssetEntry& right) {
                return left.metadata.source_path.generic_string() <
                       right.metadata.source_path.generic_string();
            });
    return entries;
}

std::vector<arti::asset::AssetMetadata> AssetPipeline::allMetadata() const {
    if (!isOpen()) {
        return {};
    }
    auto metadata = manager().catalog().allMetadata();
    std::ranges::sort(metadata,
            [](const arti::asset::AssetMetadata& left, const arti::asset::AssetMetadata& right) {
                return std::tuple{ left.source_path.generic_string(), left.type,
                           left.handle.value() } <
                       std::tuple{ right.source_path.generic_string(), right.type,
                           right.handle.value() };
            });
    return metadata;
}

arti::asset::AssetManager& AssetPipeline::manager() { return m_runtime.manager(); }

const arti::asset::AssetManager& AssetPipeline::manager() const { return m_runtime.manager(); }

} // namespace arti::tools::asset
