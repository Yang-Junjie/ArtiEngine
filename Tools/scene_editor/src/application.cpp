#include "artichoco/core/application.h"
#include "artichoco/platform/window/window_factory.h"
#include "editor_layer.h"

#include <memory>

namespace arti::core {

Application* createApplication(int argc, char** argv) {
    ApplicationCreateInfo info;
    info.name = "ArtiEngine Scene Editor";
    info.log_channel = "Editor";
    info.width = 1'600;
    info.height = 900;
    info.window_factory = platform::createSDLWindow;

    auto* app = new Application(info);
    app->pushLayer(std::make_unique<editor::EditorLayer>());
    return app;
}

} // namespace arti::core
