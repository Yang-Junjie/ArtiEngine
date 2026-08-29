#pragma once
#include "asset/importers/detail/engine_importer.h"

namespace arti::engine::asset {

// glTF 2.0（.gltf / .glb）的复合导入器。一次导入产出这个文件里全部的子资产：
// 图片 → TextureAsset，材质 → MaterialAsset（恒为 PBR），mesh → MeshAsset
// （primitive 一对一映射成 submesh），节点树 → PrefabAsset。
//
// 和 ObjImporter 的关键差别：glTF 原生就是 metallic-roughness，材质映射不用像 MTL 那样
// 靠字段有没有值去猜；节点层级是真的层级，不是 OBJ 那样凑出来的两层结构。
class GltfImporter final : public detail::EngineImporter {
public:
    std::vector<std::string> getSupportedExtensions() const override;

    arti::asset::AssetImportResult import(const std::filesystem::path& source_path) override;

private:
    std::vector<std::byte> encode(const arti::asset::AssetMetadata& metadata,
            const std::filesystem::path& source_path) const override;
};

} // namespace arti::engine::asset
