#include "asset/material_asset.h"

namespace arti::engine::asset {

MaterialAsset::MaterialAsset(core::UUID handle, Params params)
        : Asset(handle),
          m_params(params) {}

MaterialAsset::MaterialAsset(core::UUID handle, Params params,
        std::vector<std::shared_ptr<arti::asset::Asset>> dependencies)
        : Asset(handle),
          m_params(params),
          m_dependencies(std::move(dependencies)) {}

arti::asset::AssetType MaterialAsset::getType() const { return std::string{ kMaterialAssetType }; }

rendering::Material MaterialAsset::toRenderMaterial() const {
    rendering::Material material;
    material.type = m_params.type;
    material.base_color = m_params.base_color;
    material.metallic_strength = m_params.metallic_strength;
    material.roughness_strength = m_params.roughness_strength;
    material.occlusion_strength = m_params.occlusion_strength;
    material.emissive_strength = m_params.emissive_strength;
    return material;
}

}
