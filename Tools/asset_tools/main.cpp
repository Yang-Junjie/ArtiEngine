#include "asset_tools/asset_packer.h"
#include "asset_tools/asset_pipeline.h"

#include "artichoco/core/log.h"
#include "artichoco/project/project_manager.h"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void printUsage() {
    std::cerr << "Usage:\n"
              << "  asset_tools import  <project.artiproj> <Assets-relative source>\n"
              << "  asset_tools plan    <project.artiproj>   (dry run, writes nothing)\n"
              << "  asset_tools scan    <project.artiproj>   (plan + apply)\n"
              << "  asset_tools list    <project.artiproj>\n"
              << "  asset_tools validate <project.artiproj>\n"
              << "  asset_tools settings <project.artiproj> <source>\n"
              << "  asset_tools set     <project.artiproj> <source> <key> <value|->\n"
              << "  asset_tools extract <project.artiproj> <material-uuid> [destination]\n"
              << "  asset_tools pack    <project.artiproj> <output-dir> "
                 "[--overwrite] [--no-reconcile]\n";
}

std::string_view layerName(arti::asset::SettingLayer layer) {
    switch (layer) {
    case arti::asset::SettingLayer::Authored:
        return "authored";
    case arti::asset::SettingLayer::Inferred:
        return "inferred";
    case arti::asset::SettingLayer::Default:
        return "default";
    }
    return "?";
}

std::string_view actionName(arti::asset::ReconcileAction action) {
    switch (action) {
    case arti::asset::ReconcileAction::Import:
        return "import";
    case arti::asset::ReconcileAction::Reimport:
        return "reimport";
    case arti::asset::ReconcileAction::Current:
        return "current";
    case arti::asset::ReconcileAction::Unsupported:
        return "unsupported";
    }
    return "?";
}

int printPlan(const arti::asset::ReconcilePlan& plan) {
    if (!plan.complete()) {
        std::cerr << "Plan failed: " << plan.traversal_error << '\n';
        return 1;
    }

    for (const auto& item: plan.items) {
        if (item.action == arti::asset::ReconcileAction::Current) {
            continue;
        }
        std::cout << actionName(item.action) << '\t' << item.source_path.generic_string();
        if (!item.reason.empty()) {
            std::cout << "\t(" << item.reason << ')';
        }
        std::cout << '\n';
    }
    for (const auto& orphan: plan.orphans) {
        std::cout << "orphan\t" << orphan.metadata.source_path.generic_string() << '\t'
                  << orphan.metadata.handle.toString() << '\n';
    }
    for (const auto& conflict: plan.conflicts) {
        std::cerr << "conflict\t" << conflict.handle.toString() << "\tkept "
                  << conflict.kept_source.generic_string() << ", rejected "
                  << conflict.rejected_source.generic_string() << '\n';
    }
    // 一个源文件只有一份设置，所以同一张图被不同用途引用时只能有一个胜出。
    for (const auto& conflict: plan.inference_conflicts) {
        std::cerr << "setting-conflict\t" << conflict.target_source.generic_string() << '\t'
                  << conflict.key << "\tkept " << conflict.winner.generic_string() << " ("
                  << conflict.winner_usage << "), ignored " << conflict.loser.generic_string()
                  << " (" << conflict.loser_usage << ")\n";
    }
    for (const auto& cycle: plan.dependency_cycles) {
        std::cerr << "dependency-cycle\t" << cycle.generic_string() << '\n';
    }
    for (const auto& issue: plan.metadata_issues) {
        std::cerr << "bad-meta\t" << issue.meta_file.generic_string() << '\t' << issue.detail
                  << '\n';
    }

    std::cout << "Sources: " << plan.items.size()
              << ", import: " << plan.countWithAction(arti::asset::ReconcileAction::Import)
              << ", reimport: " << plan.countWithAction(arti::asset::ReconcileAction::Reimport)
              << ", current: " << plan.countWithAction(arti::asset::ReconcileAction::Current)
              << ", unsupported: "
              << plan.countWithAction(arti::asset::ReconcileAction::Unsupported)
              << ", orphans: " << plan.orphans.size() << ", conflicts: " << plan.conflicts.size()
              << ", setting conflicts: " << plan.inference_conflicts.size()
              << ", bad metadata: " << plan.metadata_issues.size() << '\n';
    return plan.conflicts.empty() && plan.metadata_issues.empty() ? 0 : 1;
}

int run(int argc, char** argv) {
    if (argc < 3) {
        printUsage();
        return 2;
    }

    const std::string_view command{ argv[1] };
    if (command != "import" && command != "plan" && command != "scan" && command != "list" &&
            command != "validate" && command != "settings" && command != "set" &&
            command != "extract" && command != "pack") {
        printUsage();
        return 2;
    }
    // pack 的参数是变长的（输出目录之后还能跟开关），所以不走下面那套固定个数的校验。
    if (command == "pack") {
        if (argc < 4) {
            printUsage();
            return 2;
        }
    } else {
        const int expected = command == "extract" ? (argc == 5 ? 5 : 4)
                : command == "import" || command == "settings" ? 4
                : command == "set"                                       ? 6
                                                                         : 3;
        if (argc != expected) {
            printUsage();
            return 2;
        }
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

    if (command == "pack") {
        arti::tools::asset::PackOptions options;
        options.output_dir = std::filesystem::path{ argv[3] };
        for (int index = 4; index < argc; ++index) {
            const std::string_view flag{ argv[index] };
            if (flag == "--overwrite") {
                options.overwrite = true;
            } else if (flag == "--no-reconcile") {
                options.reconcile = false;
            } else {
                std::cerr << "Unknown pack option: " << flag << '\n';
                printUsage();
                return 2;
            }
        }

        const auto report = arti::tools::asset::pack(pipeline, options);
        for (const auto& error: report.errors) {
            std::cerr << "error: " << error << '\n';
        }
        if (!report.succeeded) {
            return 1;
        }
        std::cout << "Packed " << report.assets << " asset(s), " << report.artifacts_copied
                  << " artifact(s), " << report.scenes_copied << " scene(s) into "
                  << report.output_dir.generic_string() << '\n'
                  << "  " << report.project_file.filename().generic_string() << '\n'
                  << "  " << report.manifest_file.filename().generic_string() << '\n'
                  << "Copy arti_player.exe into that directory to run it." << '\n';
        return 0;
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

    if (command == "settings") {
        const auto settings = pipeline.sourceSettings(std::filesystem::path{ argv[3] });
        if (!settings.valid) {
            std::cerr << "No importer claims '" << argv[3] << "'\n";
            return 1;
        }
        if (settings.schema.empty()) {
            std::cout << "This importer declares no settings\n";
            return 0;
        }
        for (const auto& descriptor: settings.schema) {
            std::cout << descriptor.key << '\t';
            if (const auto* text =
                            std::get_if<std::string>(&settings.resolved.values().at(descriptor.key))) {
                std::cout << *text;
            } else {
                std::cout << "(non-string)";
            }
            std::cout << '\t' << layerName(settings.resolved.layerOf(descriptor.key));
            if (!descriptor.allowed.empty()) {
                std::cout << "\t[";
                for (size_t index = 0; index < descriptor.allowed.size(); ++index) {
                    std::cout << (index > 0 ? "|" : "") << descriptor.allowed[index];
                }
                std::cout << ']';
            }
            std::cout << '\n';
        }
        for (const auto& issue: settings.resolved.issues()) {
            std::cerr << "issue\t" << issue.key << '\t' << issue.detail << '\n';
        }
        return 0;
    }

    if (command == "set") {
        const std::filesystem::path source{ argv[3] };
        const std::string key{ argv[4] };
        const std::string_view raw{ argv[5] };
        // "-" 表示清除用户设定，回落到 inferred / default。
        const std::optional<arti::asset::Value> value =
                raw == "-" ? std::nullopt
                           : std::optional<arti::asset::Value>{ std::string{ raw } };
        if (!pipeline.setAuthoredSetting(source, key, value)) {
            std::cerr << "Failed to update '" << key << "' on '" << source.generic_string()
                      << "'\n";
            return 1;
        }
        std::cout << "Updated " << key << " on " << source.generic_string() << '\n';
        return 0;
    }

    if (command == "extract") {
        const auto handle = arti::core::UUID::fromString(argv[3]);
        if (!handle) {
            std::cerr << "Not a valid asset UUID: " << argv[3] << '\n';
            return 2;
        }
        const std::filesystem::path destination = argc == 5 ? std::filesystem::path{ argv[4] }
                                                            : std::filesystem::path{};
        const auto extracted = pipeline.extractMaterial(*handle, destination);
        if (!extracted.succeeded) {
            std::cerr << "Extract failed: " << extracted.error << '\n';
            return 1;
        }
        std::cout << "Extracted " << argv[3] << " -> "
                  << extracted.source_path.generic_string() << " ("
                  << extracted.handle.toString() << ")\n";
        return 0;
    }

    if (command == "plan") {
        return printPlan(pipeline.planReconcile());
    }

    if (command == "scan") {
        const auto plan = pipeline.planReconcile();
        if (!plan.complete()) {
            std::cerr << "Scan failed: " << plan.traversal_error << '\n';
            return 1;
        }
        const auto report = pipeline.manager().applyReconcile(plan);
        std::cout << "Imported: " << report.imported << ", reimported: " << report.reimported
                  << ", current: " << report.current << ", unsupported: " << report.unsupported
                  << ", failed: " << report.failed << ", forgotten: " << report.forgotten
                  << " (removed " << report.removed_metadata << " .meta, "
                  << report.removed_artifacts << " artifact(s))\n";
        for (const auto& error: report.errors) {
            std::cerr << "error: " << error << '\n';
        }
        return report.succeeded() ? 0 : 1;
    }

    if (command == "list") {
        for (const auto& entry: pipeline.engineAssets()) {
            std::cout << entry.metadata.handle.toString() << '\t' << entry.metadata.type
                      << "\tengine\t" << entry.metadata.source_path.generic_string() << '\n';
        }
        for (const auto& entry: pipeline.allMetadata()) {
            if (pipeline.manager().catalog().originOf(entry.handle) ==
                    arti::asset::AssetOrigin::Engine) {
                continue;
            }
            std::cout << entry.handle.toString() << '\t' << entry.type << "\tuser\t"
                      << entry.source_path.generic_string() << '\n';
        }
        return 0;
    }

    const auto integrity = pipeline.checkIntegrity();
    for (const auto& issue: integrity.issues) {
        std::cerr << issue.handle.toString() << ": " << issue.message << '\n';
    }
    std::cout << "Checked " << integrity.assets_checked << " asset(s), found "
              << integrity.issues.size() << " issue(s)\n";
    return integrity.succeeded() ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    arti::core::Logger::init();
    const int result = run(argc, argv);
    arti::core::Logger::shutdown();
    return result;
}
