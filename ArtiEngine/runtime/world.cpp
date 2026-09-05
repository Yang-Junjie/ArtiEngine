#include "runtime/world.h"

#include "engine_log.h"
#include "physics/physics_system.h"
#include "scene/component_registration.h"
#include "script/script_system.h"

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
    // 脚本是 Update 的第一个消费者，同样**只注册这一处**。Edit 模式不调 World::tick()，
    // 所以「编辑期不跑脚本」是免费的，不需要再加开关。
    m_scene->addSystem<ScriptSystem>(scene::SystemStage::Update);
}

void World::setAssets(arti::asset::AssetManager* assets) noexcept {
    m_assets = assets;
    m_scene->getSystem<ScriptSystem>().setAssets(assets);
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
        // 一个固定步的三段顺序，**每一段的位置都是有理由的**：
        //
        //   1. 物理把 body 和场景对齐 —— 新实体现在就有 body，所以第 2 步能对它施力。
        //      少了这一段，会话第一个固定步里脚本的输入会掉在地上（那时候还没有 body）。
        //   2. 脚本的固定回调 —— 施的力、设的速度、写的运动学目标都要在第 3 步的
        //      b3World_Step 里生效，晚一步就是可见的延迟。
        //   3. FixedUpdate 阶段（物理再同步一次并解算）。
        //
        // ScriptSystem 不注册成第二个 FixedUpdate 系统，而是在这里显式调 —— 那样会多出第二个
        // 实例和第二份 sol::state，禁用状态各跑一半（见 ScriptSystem::onFixedUpdate 的注释）。
        //
        // 两处都过一遍 isSystemEnabled：这两句绕开了 runSystems，不查的话
        // setSystemEnabled<PhysicsSystem>(false) 会变成「不解算但照样建 body」这种半开状态。
        // 它同时也是「系统在不在」的检查（没注册就返回 false）。
        if (m_scene->isSystemEnabled<PhysicsSystem>()) {
            m_scene->getSystem<PhysicsSystem>().syncBodies(*m_scene, fixed_delta);
        }
        if (m_scene->isSystemEnabled<ScriptSystem>()) {
            m_scene->getSystem<ScriptSystem>().onFixedUpdate(*m_scene, context);
        }
        m_scene->runSystems(scene::SystemStage::FixedUpdate, context);
    });

    m_scene->runSystems(scene::SystemStage::Update, context);
    m_scene->runSystems(scene::SystemStage::LateUpdate, context);
}

void World::resetClock() noexcept {
    m_fixed_accumulator = core::FixedTimestepAccumulator{};
    m_frame_index = 0;
    // 时钟归零就是一次新会话的开始，所以**显式**告诉两个有会话状态的系统，而不是让它们各自
    // 从帧号回退里猜（D5）。只跑了一帧就 Stop / Play 的话两次帧号都是 0，`0 < 0` 不成立 ——
    // 旧的物理世界和旧的 Lua VM 会被原样继承下来。
    //
    // 两个 request 都只是置一个标志（noexcept），真正的重建发生在下一次派发 / 同步之前：
    // 这个函数在编辑器的 Stop / Play 路径上，不该在这里做分配和 lua_close。
    //
    // hasSystem 的判断不是形式主义：这个函数是 noexcept，而 getSystem 找不到会抛 —— 那样就是
    // std::terminate 而不是异常。构造函数保证两个都在，但 Scene::removeSystem 是公开的。
    if (m_scene->hasSystem<PhysicsSystem>()) {
        m_scene->getSystem<PhysicsSystem>().requestSessionReset();
    }
    if (m_scene->hasSystem<ScriptSystem>()) {
        m_scene->getSystem<ScriptSystem>().requestSessionReset();
    }
}

} // namespace arti::engine
