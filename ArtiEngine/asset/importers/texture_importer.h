#pragma once
#include "asset/importers/detail/engine_importer.h"

namespace arti::engine::asset {

// 独立图片文件（.png/.jpg/.../.hdr）→ TextureAsset。stb_image 负责解码。
class TextureImporter final : public detail::EngineImporter {
public:
    std::vector<std::string> getSupportedExtensions() const override;

    arti::asset::AssetImportResult import(const std::filesystem::path& source_path) override;

private:
    // format 属性从已有的 .meta 里继承，这样用户手改过颜色空间重导不会丢。
    std::string formatFromExistingMetadata(
            const std::optional<arti::asset::AssetMetadata>& existing) const;

    std::vector<std::byte> encode(const arti::asset::AssetMetadata& metadata,
            const std::filesystem::path& source_path) const override;
};

} // namespace arti::engine::asset
