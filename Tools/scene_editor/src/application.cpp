#include "artichoco/core/application.h"
#include "artichoco/platform/window/window_factory.h"
#include "editor_layer.h"

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

    bool vsync = true;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{ argv[index] };
        if (argument == "--no-vsync") {
            vsync = false;
        }
    }

    auto* app = new Application(info);
    app->pushLayer(std::make_unique<editor::EditorLayer>(vsync));
    return app;
}

} // namespace arti::core
