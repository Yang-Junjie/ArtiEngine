#pragma once
#include "artichoco/asset/asset_loader.h"

namespace arti::engine::asset {

class PrefabLoader final : public arti::asset::AssetLoader {
public:
    arti::asset::AssetType getType() const override;

private:
    std::shared_ptr<arti::asset::Asset> decode(const arti::asset::AssetMetadata& metadata,
            const std::filesystem::path& artifact_file,
            std::span<const std::shared_ptr<arti::asset::Asset>> dependencies) override;
};

}
