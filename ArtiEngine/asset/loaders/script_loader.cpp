#include "asset/loaders/script_loader.h"

#include "asset/detail/artifact_io.h"

namespace arti::engine::asset {

arti::asset::AssetType ScriptLoader::getType() const { return std::string{ kScriptAssetType }; }

std::shared_ptr<arti::asset::Asset> ScriptLoader::decode(const arti::asset::AssetMetadata& metadata,
        const std::filesystem::path& artifact_file,
        std::span<const std::shared_ptr<arti::asset::Asset>> dependencies) {
    (void)dependencies;
    return std::make_shared<ScriptAsset>(metadata.handle, detail::readTextFile(artifact_file));
}

} // namespace arti::engine::asset
