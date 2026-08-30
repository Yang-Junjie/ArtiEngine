#include "asset_tools/asset_pipeline.h"

#include "artichoco/core/log.h"
#include "artichoco/core/uuid.h"
#include "asset/prefab_asset.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "asset_pipeline_smoke: " << message << '\n';
    }
    return condition;
}

bool writeText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output << text;
    return output.good();
}

int run() {
    TemporaryDirectory temporary{ std::filesystem::temp_directory_path() /
                                  ("ArtiAssetPipeline-" +
                                          arti::core::UUID::generate().toString()) };
    const auto assets = temporary.path / "Assets";
    const auto artifacts = temporary.path / "Artifacts";
    std::error_code error;
    std::filesystem::create_directories(assets, error);
    if (!require(!error, "failed to create the temporary Assets directory")) {
        return 1;
    }

    arti::tools::asset::AssetPipeline pipeline;
    if (!require(pipeline.open(assets, artifacts), "failed to open the asset pipeline")) {
        return 1;
    }
    if (!require(pipeline.metadata().size() == 2, "builtin assets were not registered")) {
        return 1;
    }
    if (!require(pipeline.canImport("triangle.OBJ"),
                "OBJ extension matching is not case-insensitive") ||
            !require(!pipeline.canImport("notes.txt"), "unsupported extension was accepted")) {
        return 1;
    }

    constexpr std::string_view triangle = "o Triangle\n"
                                          "v 0 0 0\n"
                                          "v 1 0 0\n"
                                          "v 0 1 0\n"
                                          "vt 0 0\n"
                                          "vt 1 0\n"
                                          "vt 0 1\n"
                                          "vn 0 0 1\n"
                                          "f 1/1/1 2/2/1 3/3/1\n";
    if (!require(writeText(assets / "triangle.obj", triangle), "failed to write OBJ fixture") ||
            !require(writeText(assets / "notes.txt", "unsupported\n"),
                    "failed to write unsupported fixture")) {
        return 1;
    }

    const auto imported = pipeline.importFile("triangle.obj");
    if (!require(imported.succeeded(), "OBJ import failed") ||
            !require(imported.output_count > 0, "OBJ import produced no assets") ||
            !require(pipeline.isImported("triangle.obj"), "imported source was not indexed")) {
        return 1;
    }

    constexpr std::array<std::string_view, 1> preferred{ arti::engine::asset::kPrefabAssetType };
    const auto primary = pipeline.primaryAsset("triangle.obj", preferred);
    if (!require(primary.has_value(), "primary asset lookup failed") ||
            !require(primary->type == arti::engine::asset::kPrefabAssetType,
                    "primary asset preference was ignored")) {
        return 1;
    }

    const auto scan = pipeline.importPending();
    if (!require(scan.succeeded(), "pending scan failed") ||
            !require(scan.imported_files == 0, "pending scan reimported a current source") ||
            !require(scan.current_files == 1, "pending scan did not identify the current OBJ") ||
            !require(scan.unsupported_files == 1,
                    "pending scan did not identify the unsupported file")) {
        return 1;
    }
    if (!require(pipeline.validate().succeeded(), "freshly imported assets failed validation")) {
        return 1;
    }

    pipeline.close();
    if (!require(pipeline.open(assets, artifacts), "failed to reopen the asset pipeline") ||
            !require(pipeline.isImported("triangle.obj"),
                    "persisted metadata was not restored after reopen")) {
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    arti::core::Logger::init();
    const int result = run();
    arti::core::Logger::shutdown();
    return result;
}
