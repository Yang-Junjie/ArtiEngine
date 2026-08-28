#include "scene/component_registration.h"

#include "artichoco/scene/scene.h"
#include "scene/components.h"

namespace arti::engine {

void registerSceneComponents() {
    scene::Scene::registerComponentCopy<MeshRendererComponent>();
    scene::Scene::registerComponentCopy<CameraComponent>();
    scene::Scene::registerComponentCopy<DirectionalLightComponent>();
}

} // namespace arti::engine
