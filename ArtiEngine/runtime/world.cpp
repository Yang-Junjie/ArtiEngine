#include "runtime/world.h"

#include "engine_log.h"
#include "runtime/physics_system.h"
#include "scene/component_registration.h"

#include "artichoco/scene/scene.h"
#include "artichoco/scene/scene_serializer.h"
#include "artichoco/scene/system.h"

#include <yaml-cpp/yaml.h>

#include <exception>
#include <string>

namespace arti::engine {

World::World()
        : m_scene(std::make_unique<scene::Scene>()),
          m_serialization(std::make_unique<scene::SceneSerializationRegistry>()) {
    registerSceneComponents(m_serialization.get());
    m_serializer = std::make_unique<scene::SceneSerializer>(*m_serialization);
    // 物理是 FixedUpdate 的第一个消费者。注册**只有这一处**，所以编辑器的 Play /
    // Simulate 和独立 player 跑的是同一份 —— 不会出现「编辑器里能掉、exe 里不动」（D2）。
    m_scene->addSystem<PhysicsSystem>(scene::SystemStage::FixedUpdate);
}

World::~World() = default;

scene::Scene& World::scene() noexcept { return *m_scene; }

const scene::Scene& World::scene() const noexcept { return *m_scene; }

bool World::loadScene(const std::filesystem::path& path) {
    try {
        m_serializer->load(path, *m_scene);
    } catch (const std::exception& exception) {
        getLogChannel().error("Failed to load the scene '{}': {}", path.string(), exception.what());
        m_scene->clearEntities();
        resetClock();
        return false;
    }

    resetClock();
    getLogChannel().info("Loaded the scene '{}'", path.string());
    return true;
}

bool World::saveScene(const std::filesystem::path& path) const {
    try {
        m_serializer->save(*m_scene, path);
    } catch (const std::exception& exception) {
        getLogChannel().error("Failed to save the scene '{}': {}", path.string(), exception.what());
        return false;
    }
    return true;
}

std::string World::captureScene() const {
    try {
        YAML::Emitter emitter;
        emitter << m_serializer->serialize(*m_scene);
        if (!emitter.good()) {
            getLogChannel().error("Failed to emit a scene snapshot: {}", emitter.GetLastError());
            return {};
        }
        return std::string{ emitter.c_str() };
    } catch (const std::exception& exception) {
        getLogChannel().error("Failed to capture a scene snapshot: {}", exception.what());
        return {};
    }
}

bool World::restoreScene(std::string_view text) {
    try {
        // YAML::Load 没有 string_view 的重载，这里必须实体化一份。
        m_serializer->deserialize(YAML::Load(std::string{ text }), *m_scene);
    } catch (const std::exception& exception) {
        // 刻意不清场景、不动时钟，理由见头文件。
        getLogChannel().error("Failed to restore a scene snapshot: {}", exception.what());
        return false;
    }
    return true;
}

void World::clear() {
    m_scene->clearEntities();
    resetClock();
}

void World::tick(float delta_time) {
    scene::UpdateContext context;
    context.deltaTime = delta_time;
    context.fixedDeltaTime = m_fixed_accumulator.fixedDeltaTime();
    context.frameIndex = m_frame_index++;

    m_fixed_accumulator.tick(delta_time, [this, &context](float fixed_delta) {
        context.fixedDeltaTime = fixed_delta;
        m_scene->runSystems(scene::SystemStage::FixedUpdate, context);
    });

    m_scene->runSystems(scene::SystemStage::Update, context);
    m_scene->runSystems(scene::SystemStage::LateUpdate, context);
}

void World::resetClock() noexcept {
    m_fixed_accumulator = core::FixedTimestepAccumulator{};
    m_frame_index = 0;
}

} // namespace arti::engine
