#include "artichoco/core/application.h"
#include "artichoco/platform/window/window_factory.h"
#include "editor_layer.h"

#include <charconv>
#include <memory>
#include <string_view>

namespace arti::core {

Application* createApplication(int argc, char** argv) {
    ApplicationCreateInfo info;
    info.name = "ArtiEngine Scene Editor";
    info.log_channel = "Editor";
    info.width = 1'600;
    info.height = 900;
    info.window_factory = platform::createSDLWindow;

    editor::EditorLayerOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{ argv[index] };
        if (argument == "--auto-play") {
            options.auto_play = true;
        } else if (argument == "--auto-pick") {
            options.auto_pick = true;
        } else if (argument == "--auto-project") {
            options.auto_project = true;
        } else if (argument == "--auto-scene-io") {
            options.auto_scene_io = true;
        } else if (argument == "--project" && (index + 1) < argc) {
            options.project_file = argv[++index];
        } else if (argument == "--environment" && (index + 1) < argc) {
            options.environment_source = argv[++index];
        } else if (argument == "--frames" && (index + 1) < argc) {
            const std::string_view value{ argv[++index] };
            uint32_t parsed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec == std::errc{}) {
                options.frame_limit = parsed;
            }
        } else if (!argument.starts_with("--")) {
            options.scene_path = argv[index];
        }
    }

    auto* app = new Application(info);
    app->pushLayer(std::make_unique<editor::EditorLayer>(std::move(options)));
    return app;
}

} // namespace arti::core
