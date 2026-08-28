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

    const char* scene_path = nullptr;
    uint32_t frame_limit = 0;
    bool auto_play = false;
    bool auto_pick = false;
    bool auto_project = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{ argv[index] };
        if (argument == "--auto-play") {
            auto_play = true;
        } else if (argument == "--auto-pick") {
            auto_pick = true;
        } else if (argument == "--auto-project") {
            auto_project = true;
        } else if (argument == "--frames" && (index + 1) < argc) {
            const std::string_view value{ argv[++index] };
            uint32_t parsed = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (result.ec == std::errc{}) {
                frame_limit = parsed;
            }
        } else if (!argument.starts_with("--")) {
            scene_path = argv[index];
        }
    }

    auto* app = new Application(info);
    app->pushLayer(std::make_unique<editor::EditorLayer>(scene_path, frame_limit, auto_play,
            auto_pick, auto_project));
    return app;
}

} // namespace arti::core
