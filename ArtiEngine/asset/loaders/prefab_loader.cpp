#include "asset/loaders/prefab_loader.h"

#include "asset/detail/artifact_io.h"
#include "asset/detail/prefab_artifact.h"

namespace arti::engine::asset {

arti::asset::AssetType PrefabLoader::getType() const {
    return std::string{ kPrefabAssetType };
}

std::shared_ptr<arti::asset::Asset> PrefabLoader::decode(const arti::asset::AssetMetadata& metadata,
        const std::filesystem::path& artifact_file,
        std::span<const std::shared_ptr<arti::asset::Asset>> dependencies) {
    (void)dependencies;
    return detail::decodePrefabArtifact(metadata.handle, detail::readFileBinary(artifact_file));
}

}
