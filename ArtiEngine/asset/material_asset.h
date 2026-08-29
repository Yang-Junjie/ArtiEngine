#pragma once
#include "arti_renderer.h"
#include "artichoco/asset/asset.h"

#include <string_view>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace arti::engine::asset {

inline constexpr std::string_view kMaterialAssetType{ "artiengine.asset.material" };

class TextureAsset;

class MaterialAsset final : public arti::asset::Asset {
public:
    struct Params {
        rendering::MaterialType type{ rendering::MaterialType::BlinnPhong };
        glm::vec4 base_color{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec3 specular_color{ 1.0f, 1.0f, 1.0f };
        float specular_strength{ 0.5f };
        float shininess{ 32.0f };
        float metallic_strength{ 0.0f };
        float roughness_strength{ 1.0f };
        float occlusion_strength{ 1.0f };
        float emissive_strength{ 0.0f };

        arti::asset::AssetHandle<TextureAsset> base_color_texture;
        arti::asset::AssetHandle<TextureAsset> metallic_roughness_texture;
        arti::asset::AssetHandle<TextureAsset> normal_texture;
        arti::asset::AssetHandle<TextureAsset> occlusion_texture;
        arti::asset::AssetHandle<TextureAsset> emissive_texture;
    };

    MaterialAsset(core::UUID handle, Params params);

    arti::asset::AssetType getType() const override;
    const Params& params() const noexcept { return m_params; }

    rendering::Material toRenderMaterial() const;

private:
    Params m_params;
};

}
