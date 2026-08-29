#include "scene/component_registration.h"

#include "artichoco/scene/scene.h"
#include "scene/component_serialization.h"
#include "scene/components.h"

namespace arti::engine {

void registerSceneComponents(arti::scene::SceneSerializationRegistry* registry) {
    scene::Scene::registerComponentCopy<MeshRendererComponent>();
    scene::Scene::registerComponentCopy<CameraComponent>();
    scene::Scene::registerComponentCopy<DirectionalLightComponent>();

    if (registry != nullptr) {
        registerSceneSerialization(*registry);
    }
}

} // namespace arti::engine
