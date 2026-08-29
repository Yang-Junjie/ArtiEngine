#pragma once
#include "asset/importers/detail/engine_importer.h"

namespace arti::engine::asset {

class TextureImporter final : public detail::EngineImporter {
public:
    std::vector<std::string> getSupportedExtensions() const override;

    arti::asset::AssetImportResult import(const std::filesystem::path& source_path) override;

private:
    std::string formatFromExistingMetadata(
            const std::optional<arti::asset::AssetMetadata>& existing) const;

    std::vector<std::byte> encode(const arti::asset::AssetMetadata& metadata,
            const std::filesystem::path& source_path) const override;
};

}
