#pragma once
#include "asset/importers/detail/engine_importer.h"

namespace arti::engine::asset {

class ObjImporter final : public detail::EngineImporter {
public:
    std::vector<std::string> getSupportedExtensions() const override;

    arti::asset::AssetImportResult import(const std::filesystem::path& source_path) override;

private:
    std::vector<std::byte> encode(const arti::asset::AssetMetadata& metadata,
            const std::filesystem::path& source_path) const override;
};

} // namespace arti::engine::asset
