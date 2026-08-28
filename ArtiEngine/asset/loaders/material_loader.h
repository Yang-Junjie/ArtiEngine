#pragma once
#include "artichoco/asset/asset_loader.h"

#include "asset/material_asset.h"

namespace arti::engine::asset {

class MaterialLoader final : public arti::asset::AssetLoader {
public:
    arti::asset::AssetType getType() const override;

private:
    std::shared_ptr<arti::asset::Asset> decode(const arti::asset::AssetMetadata& metadata,
            const std::filesystem::path& artifact_file,
            std::span<const std::shared_ptr<arti::asset::Asset>> dependencies) override;
};

// 材质的 artifact 是 YAML（参数少、可读、手改方便），不像网格那样走二进制。
std::vector<std::byte> encodeMaterialArtifact(const MaterialAsset::Params& params);

} // namespace arti::engine::asset
