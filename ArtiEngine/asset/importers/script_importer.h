#pragma once
#include "asset/importers/detail/engine_importer.h"

namespace arti::engine::asset {

// `.lua` → `.artiscript`。没有设置、没有子资产、没有跨源引用：一个文件就是一份脚本。
class ScriptImporter final : public detail::EngineImporter {
public:
    std::vector<std::string> getSupportedExtensions() const override;
    std::string getName() const override { return "artiengine.ScriptImporter"; }

    arti::asset::AssetImportResult import(
            const arti::asset::AssetImportRequest& request) override;
};

} // namespace arti::engine::asset
