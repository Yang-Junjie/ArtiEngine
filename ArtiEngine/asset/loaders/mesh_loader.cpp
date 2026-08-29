#include "asset/loaders/mesh_loader.h"

#include "asset/detail/artifact_io.h"
#include "asset/detail/mesh_artifact.h"

namespace arti::engine::asset {

arti::asset::AssetType MeshLoader::getType() const { return std::string{ kMeshAssetType }; }

std::shared_ptr<arti::asset::Asset> MeshLoader::decode(const arti::asset::AssetMetadata& metadata,
        const std::filesystem::path& artifact_file,
        std::span<const std::shared_ptr<arti::asset::Asset>> dependencies) {
    (void) dependencies;
    return detail::decodeMeshArtifact(metadata.handle, detail::readFileBinary(artifact_file));
}

}
