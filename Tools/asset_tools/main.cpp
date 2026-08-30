#include "asset_tools/asset_pipeline.h"

#include "artichoco/core/log.h"
#include "artichoco/project/project_manager.h"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void printUsage() {
    std::cerr << "Usage:\n"
              << "  asset_tools import <project.artiproj> <Assets-relative source>\n"
              << "  asset_tools scan <project.artiproj>\n"
              << "  asset_tools list <project.artiproj>\n"
              << "  asset_tools validate <project.artiproj>\n";
}

int run(int argc, char** argv) {
    if (argc < 3) {
        printUsage();
        return 2;
    }

    const std::string_view command{ argv[1] };
    if ((command == "import" && argc != 4) || (command != "import" && argc != 3)) {
        printUsage();
        return 2;
    }
    if (command != "import" && command != "scan" && command != "list" && command != "validate") {
        printUsage();
        return 2;
    }

    auto& projects = arti::project::ProjectManager::instance();
    if (!projects.loadProject(std::filesystem::path{ argv[2] })) {
        std::cerr << "Failed to load project: " << argv[2] << '\n';
        return 1;
    }
    const auto assets_root = projects.getAssetsRootPath();
    const auto artifacts_root = projects.getArtifactsRootPath();
    if (!assets_root || !artifacts_root) {
        std::cerr << "Project does not define Assets and Artifacts roots\n";
        return 1;
    }

    arti::tools::asset::AssetPipeline pipeline;
    if (!pipeline.open(*assets_root, *artifacts_root)) {
        std::cerr << "Failed to open the asset workspace\n";
        return 1;
    }

    if (command == "import") {
        const std::filesystem::path source{ argv[3] };
        const auto summary = pipeline.importFile(source);
        if (!summary.succeeded()) {
            std::cerr << "Import failed for '" << source.generic_string() << "': " << summary.error
                      << '\n';
            return 1;
        }
        std::cout << "Imported " << source.generic_string() << " -> " << summary.output_count
                  << " asset(s)\n";
        return 0;
    }

    if (command == "scan") {
        const auto summary = pipeline.importPending();
        std::cout << "Sources: " << summary.source_files << ", imported: " << summary.imported_files
                  << ", current: " << summary.current_files
                  << ", unsupported: " << summary.unsupported_files
                  << ", failed: " << summary.failed_files << '\n';
        if (!summary.succeeded()) {
            if (!summary.traversal_error.empty()) {
                std::cerr << "Scan failed: " << summary.traversal_error << '\n';
            }
            return 1;
        }
        return 0;
    }

    if (command == "list") {
        for (const auto& entry: pipeline.metadata()) {
            std::cout << entry.handle.toString() << '\t' << entry.type << '\t'
                      << entry.source_path.generic_string() << '\n';
        }
        return 0;
    }

    const auto validation = pipeline.validate();
    for (const auto& issue: validation.issues) {
        std::cerr << issue.asset.toString() << ": " << issue.message << '\n';
    }
    std::cout << "Checked " << validation.assets_checked << " asset(s), found "
              << validation.issues.size() << " issue(s)\n";
    return validation.succeeded() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    arti::core::Logger::init();
    const int result = run(argc, argv);
    arti::core::Logger::shutdown();
    return result;
}
