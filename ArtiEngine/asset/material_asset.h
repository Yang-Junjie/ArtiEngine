#pragma once
#include "arti_renderer.h"
#include "artichoco/asset/asset.h"

#include <string_view>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace arti::engine::asset {

inline constexpr std::string_view kMaterialAssetType{ "artiengine.asset.material" };

// 材质的**参数**。纹理还没做成资产，所以这一版没有纹理引用 ——
// 加上去的时候走 AssetMetadata::dependencies，loader 会拿到已经加载好的 TextureAsset。
//
// 字段刻意对着 rendering::Material 摆：那边是渲染要的形状，这边是磁盘上的形状，
// 两者一一对应，转换就不需要判断和映射表。
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
    };

    MaterialAsset(core::UUID handle, Params params);

    arti::asset::AssetType getType() const override;

    const Params& params() const noexcept { return m_params; }

  
    rendering::Material toRenderMaterial() const;

private:
    Params m_params;
};

} // namespace arti::engine::asset
