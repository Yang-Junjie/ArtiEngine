#pragma once
#include "asset/importers/detail/engine_importer.h"

namespace arti::engine::asset {

class ObjImporter final : public detail::EngineImporter {
public:
    std::vector<std::string> getSupportedExtensions() const override;
    std::string getName() const override { return "artiengine.ObjImporter"; }

    // 声明 mtl 里引用的贴图，并按 map_* 槽位推断它们的 Colorspace。
    arti::asset::SourcePrescan prescan(
            const std::filesystem::path& source_path) const override;

    arti::asset::AssetImportResult import(
            const arti::asset::AssetImportRequest& request) override;
};

}
