#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace arti::engine::asset::detail {

// OBJ 的 mtl 里引用的一张贴图，以及它的用途。
struct MtlTextureUsage {
    std::filesystem::path file;  // 绝对路径
    std::string usage;           // "base_color" / "normal" / ...
    bool is_color{ false };      // 颜色数据 → sRGB；数据贴图 → linear
};

// 只做文本扫描：从 OBJ 里读 mtllib，再从那些 mtl 里读 map_* 行。
// 不解析几何、不打开图片。prescan 在 plan 阶段跑，必须便宜。
//
// 用途映射与 ObjImporter::import 里的一致（obj_importer.cpp 的 importTexture 调用）：
//   map_Kd → base_color(sRGB)      norm/map_Bump/bump → normal(linear)
//   map_Pr → metallic_roughness    map_Pm → metallic_roughness(linear)
//   map_Ka → occlusion(linear)     map_Ke → emissive(sRGB)
std::vector<MtlTextureUsage> scanMtlTextures(const std::filesystem::path& obj_file);

}
