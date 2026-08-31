#include "asset/importers/detail/mtl_scan.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>

namespace arti::engine::asset::detail {
namespace {

struct SlotMapping {
    std::string_view keyword;
    std::string_view usage;
    bool is_color;
};

// 与 ObjImporter::import 的槽位映射保持一致。顺序无关，keyword 精确匹配。
constexpr std::array<SlotMapping, 8> kSlots{ {
        { "map_Kd", "base_color", true },
        { "map_Ke", "emissive", true },
        { "norm", "normal", false },
        { "map_Bump", "normal", false },
        { "bump", "normal", false },
        { "map_Pr", "metallic_roughness", false },
        { "map_Pm", "metallic_roughness", false },
        { "map_Ka", "occlusion", false },
} };

// map_* 行可能带 -bm / -o / -s 之类选项，文件名是最后一个非选项 token。
// 选项都以 '-' 开头且带参数，简单起见取最后一个不以 '-' 开头、且前一个 token
// 也不是以 '-' 开头的选项名的 token。
std::string textureNameFrom(std::string_view rest) {
    std::vector<std::string> tokens;
    std::istringstream stream{ std::string{ rest } };
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    if (tokens.empty()) {
        return {};
    }
    // 绝大多数情况下就是唯一 token；带选项时文件名在末尾。
    return tokens.back();
}

std::vector<std::string> readMtlLibs(const std::filesystem::path& obj_file) {
    std::vector<std::string> libs;
    std::ifstream input{ obj_file };
    if (!input.is_open()) {
        return libs;
    }
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream stream{ line };
        std::string keyword;
        if (!(stream >> keyword) || keyword != "mtllib") {
            continue;
        }
        std::string name;
        while (stream >> name) {
            if (std::ranges::find(libs, name) == libs.end()) {
                libs.push_back(name);
            }
        }
    }
    return libs;
}

}

std::vector<MtlTextureUsage> scanMtlTextures(const std::filesystem::path& obj_file) {
    std::vector<MtlTextureUsage> usages;
    const std::filesystem::path base = obj_file.parent_path();

    for (const std::string& lib: readMtlLibs(obj_file)) {
        const std::filesystem::path mtl_file = (base / lib).lexically_normal();
        std::ifstream input{ mtl_file };
        if (!input.is_open()) {
            continue;
        }

        std::string line;
        while (std::getline(input, line)) {
            std::istringstream stream{ line };
            std::string keyword;
            if (!(stream >> keyword)) {
                continue;
            }
            const auto slot = std::ranges::find_if(kSlots,
                    [&keyword](const SlotMapping& mapping) { return mapping.keyword == keyword; });
            if (slot == kSlots.end()) {
                continue;
            }

            std::string rest;
            std::getline(stream, rest);
            const std::string name = textureNameFrom(rest);
            if (name.empty()) {
                continue;
            }

            const auto file = (base / std::filesystem::path{ name }).lexically_normal();
            std::error_code error;
            if (!std::filesystem::is_regular_file(file, error) || error) {
                continue;
            }
            // 同一张图同一用途只记一条；不同用途各记一条，冲突留给上层裁决。
            const auto existing = std::ranges::find_if(usages,
                    [&](const MtlTextureUsage& entry) {
                        return entry.file == file && entry.usage == slot->usage;
                    });
            if (existing != usages.end()) {
                continue;
            }
            usages.push_back({ file, std::string{ slot->usage }, slot->is_color });
        }
    }
    return usages;
}

}
