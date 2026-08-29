#pragma once

namespace arti::scene {
class SceneSerializationRegistry;
} // namespace arti::scene

namespace arti::engine {

void registerSceneComponents(arti::scene::SceneSerializationRegistry* registry = nullptr);

} // namespace arti::engine
