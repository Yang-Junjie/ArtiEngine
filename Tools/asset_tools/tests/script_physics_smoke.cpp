// 脚本—物理桥接的端到端冒烟：真的从 Assets/ 导入 .lua、挂在实体上、跑 World::tick，
// 断言的是**从 Lua 那一侧看到的行为**。
//
// 和两个邻居的分工：
//   script_runtime_smoke      脚本能改场景、抛错会被禁用（不碰物理）
//   physics_kinematic_smoke   C++ 侧的连续运动 / 控制接口 / body 生命周期（不碰 Lua）
//   这一个                    固定步回调的节奏、绑定层、以及「同一个固定步里施力就生效」
//
// 放在 Tools 而不是 ArtiEngine：它需要 AssetPipeline（importer 只注册在那儿）**和** World，
// 而 ArtiEngine 的测试目标链不到 ArtiTools::Asset。

#include "asset_tools/asset_pipeline.h"

#include "physics/physics_system.h"
#include "runtime/world.h"
#include "scene/components.h"

#include "artichoco/asset/asset_manager.h"
#include "artichoco/core/log.h"
#include "artichoco/core/task/task_system.h"
#include "artichoco/core/uuid.h"
#include "artichoco/scene/components.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>

namespace {

using namespace arti;

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "script_physics_smoke: " << message << '\n';
    }
    return condition;
}

bool writeText(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output << text;
    return output.good();
}

// 每种回调各记在一个轴上：on_create → y，on_fixed_update → x，on_update → z。
// 挂它的实体**不带刚体**，所以物理不会写回来盖掉这些计数。
constexpr std::string_view kCadenceSource = R"(
function on_create(entity)
    local t = entity.translation
    t.y = t.y + 1
    entity.translation = t
end

function on_fixed_update(entity, dt)
    local t = entity.translation
    t.x = t.x + 1
    entity.translation = t
end

function on_update(entity, dt)
    local t = entity.translation
    t.z = t.z + 1
    entity.translation = t
end
)";

// 第一个固定步就给一个横向速度，之后什么都不做。
// **会话的第一个固定步里 body 就必须存在** —— 不然这一下会掉在地上。
constexpr std::string_view kKickSource = R"(
local kicked = false

function on_fixed_update(entity, dt)
    if not kicked then
        kicked = arti.physics.set_linear_velocity(entity, { x = 5, y = 0, z = 0 })
    end
end
)";

// 每个固定步顶一下。30 N 顶 1 kg（密度 1 的单位立方体），扣掉重力还净剩 20 m/s²。
constexpr std::string_view kHoverSource = R"(
function on_fixed_update(entity, dt)
    arti.physics.apply_force(entity, { x = 0, y = 30, z = 0 })
end
)";

// 挂在一个**没有刚体**的实体上：三个控制调用都必须失败，而且不许崩、不许踩 Box3D 的断言。
// 每种失败各记一个轴，所以「返回了什么」直接读得出来。
constexpr std::string_view kGuardSource = R"(
function on_fixed_update(entity, dt)
    local t = entity.translation
    if arti.physics.set_linear_velocity(entity, { x = 1, y = 0, z = 0 }) == false then
        t.x = t.x + 1
    end
    if arti.physics.apply_force(entity, { x = 0, y = 1, z = 0 }) == false then
        t.y = t.y + 1
    end
    if arti.physics.get_linear_velocity(entity) == nil then
        t.z = t.z + 1
    end
    entity.translation = t
end
)";

// 固定回调里抛错。**两种回调共用一份禁用状态**，所以 on_update 也必须跟着停 ——
// 「固定步不跑了但渲染帧还在跑」比彻底不跑更难查。
constexpr std::string_view kBoomSource = R"(
function on_fixed_update(entity, dt)
    local t = entity.translation
    t.x = t.x + 1
    entity.translation = t
    error("boom from the fixed callback")
end

function on_update(entity, dt)
    local t = entity.translation
    t.z = t.z + 1
    entity.translation = t
end
)";

// 在固定回调里自杀 / 杀别人。派发必须先快照身份再逐个验证，否则这就是边遍历 view 边改存储。
constexpr std::string_view kSuicideSource = R"(
function on_fixed_update(entity, dt)
    entity:destroy()
end
)";

constexpr std::string_view kKillerSource = R"(
function on_fixed_update(entity, dt)
    local victim = arti.scene.find_by_tag("Victim")
    if victim then
        victim:destroy()
    end
end
)";

// 一个临时项目：每段 Lua 写成一个文件、reconcile 一遍，之后按名字取 handle。
struct Workspace {
    TemporaryDirectory temp;
    tools::asset::AssetPipeline pipeline;
    std::map<std::string, arti::asset::AssetHandle<engine::asset::ScriptAsset>> scripts;

    bool open() {
        const auto assets = temp.path / "Assets";
        const auto artifacts = temp.path / "Artifacts";
        std::error_code error;
        std::filesystem::create_directories(assets, error);
        std::filesystem::create_directories(artifacts, error);
        if (!require(!error, "failed to create the temporary workspace")) {
            return false;
        }

        const std::pair<std::string_view, std::string_view> sources[] = {
            { "cadence", kCadenceSource },
            { "kick", kKickSource },
            { "hover", kHoverSource },
            { "guard", kGuardSource },
            { "boom", kBoomSource },
            { "suicide", kSuicideSource },
            { "killer", kKillerSource },
        };
        for (const auto& [name, text]: sources) {
            if (!require(writeText(assets / "Scripts" / (std::string{ name } + ".lua"), text),
                        "failed to write " + std::string{ name } + ".lua")) {
                return false;
            }
        }

        if (!require(pipeline.open(assets, artifacts), "failed to open the asset pipeline") ||
                !require(pipeline.reconcile().succeeded(), "reconcile failed")) {
            return false;
        }

        for (const auto& [name, text]: sources) {
            (void)text;
            const auto imported =
                    pipeline.sourceAssets("Scripts/" + std::string{ name } + ".lua");
            if (!require(imported.assets.size() == 1,
                        std::string{ name } + ".lua did not produce exactly one asset")) {
                return false;
            }
            scripts.emplace(std::string{ name },
                    arti::asset::AssetHandle<engine::asset::ScriptAsset>{
                        imported.assets.front().handle });
        }
        return true;
    }
};

// 挂了脚本的实体。带不带刚体由调用方决定 —— 计数用的实体一定不能带（物理会写回 transform）。
core::UUID spawnScripted(scene::Scene& scene, const char* tag,
        const arti::asset::AssetHandle<engine::asset::ScriptAsset>& script,
        const glm::vec3& position = glm::vec3{ 0.0f }) {
    auto entity = scene.createEntity(tag);
    entity.getComponent<scene::TransformComponent>().translation = position;
    entity.addComponent<engine::ScriptComponent>().script = script;
    return entity.getUUID();
}

void addBody(scene::Scene& scene, core::UUID id, engine::RigidBodyComponent::Type type,
        const glm::vec3& half_extents = glm::vec3{ 0.5f }) {
    auto entity = scene.findEntity(id);
    auto& body = entity.addComponent<engine::RigidBodyComponent>();
    body.type = type;
    body.enable_sleep = false;
    auto& collider = entity.addComponent<engine::ColliderComponent>();
    collider.shape = engine::ColliderComponent::Shape::Box;
    collider.half_extents = half_extents;
}

core::UUID makeGround(scene::Scene& scene) {
    auto entity = scene.createEntity("Ground");
    entity.getComponent<scene::TransformComponent>().translation =
            glm::vec3{ 0.0f, -0.25f, 0.0f };
    auto& body = entity.addComponent<engine::RigidBodyComponent>();
    body.type = engine::RigidBodyComponent::Type::Static;
    auto& collider = entity.addComponent<engine::ColliderComponent>();
    collider.half_extents = glm::vec3{ 40.0f, 0.25f, 40.0f };
    return entity.getUUID();
}

glm::vec3 translationOf(scene::Scene& scene, core::UUID id) {
    return scene.findEntity(id).getComponent<scene::TransformComponent>().translation;
}

std::string vecText(const glm::vec3& value) {
    return "(" + std::to_string(value.x) + ", " + std::to_string(value.y) + ", " +
            std::to_string(value.z) + ")";
}

// ---- 1. 一帧里可能是零个、一个或多个固定步；on_create 只跑一次 ----
bool checkCadence(Workspace& workspace) {
    engine::World world;
    world.setAssets(&workspace.pipeline.manager());
    auto& scene = world.scene();
    const core::UUID counter = spawnScripted(scene, "Counter", workspace.scripts.at("cadence"));

    // 固定步长是 1/60 ≈ 0.016667。前两帧太短，攒不满一个固定步。
    world.tick(0.005f);
    world.tick(0.005f);
    if (!require(std::fabs(translationOf(scene, counter).x) < 1e-5f,
                "帧长不够一个固定步，on_fixed_update 却跑了 —— 计数 " +
                        vecText(translationOf(scene, counter)))) {
        return false;
    }
    if (!require(std::fabs(translationOf(scene, counter).z - 2.0f) < 1e-5f,
                "on_update 没有每帧一次 —— 计数 " + vecText(translationOf(scene, counter)))) {
        return false;
    }

    // 攒到 0.02 秒，够一个固定步了（余 0.00333）。
    world.tick(0.01f);
    if (!require(std::fabs(translationOf(scene, counter).x - 1.0f) < 1e-5f,
                "第一个固定步没跑或跑了多次 —— 计数 " +
                        vecText(translationOf(scene, counter)))) {
        return false;
    }

    // 一帧 0.11 秒（外加上一帧的余额）→ 6 个固定步。刻意不取 1/60 的整数倍，
    // 免得断言卡在 5 和 6 的边界上。
    world.tick(0.11f);
    if (!require(std::fabs(translationOf(scene, counter).x - 7.0f) < 1e-5f,
                "一帧里的多个固定步数不对 —— 计数 " +
                        vecText(translationOf(scene, counter)) + "，x 应该是 7")) {
        return false;
    }
    if (!require(std::fabs(translationOf(scene, counter).z - 4.0f) < 1e-5f,
                "on_update 跟着固定步跑了 —— 计数 " + vecText(translationOf(scene, counter)) +
                        "，z 应该是 4（每帧一次）")) {
        return false;
    }
    // **on_create 只有一次**，哪怕两种回调各跑了好几轮。
    if (!require(std::fabs(translationOf(scene, counter).y - 1.0f) < 1e-5f,
                "on_create 跑了不止一次 —— 计数 " + vecText(translationOf(scene, counter)))) {
        return false;
    }
    return true;
}

// ---- 2. 固定回调里施的力 / 设的速度，在同一个固定步里就生效 ----
//
// 这一条同时钉住 World::tick 里那个三段顺序：物理先同步 body、脚本再跑、然后才解算。
// 少了第一段，**会话的第一个固定步**里 set_linear_velocity 会因为「还没有 body」返回 false。
bool checkSameStepEffect(Workspace& workspace) {
    engine::World world;
    world.setAssets(&workspace.pipeline.manager());
    auto& scene = world.scene();
    makeGround(scene);

    const core::UUID kicked = spawnScripted(
            scene, "Kicked", workspace.scripts.at("kick"), glm::vec3{ 0.0f, 0.5f, 0.0f });
    addBody(scene, kicked, engine::RigidBodyComponent::Type::Dynamic);

    // **只跑一帧。** 5 m/s 跑一个 1/60 的步长是 0.083 米。
    world.tick(1.0f / 60.0f);
    const float x = translationOf(scene, kicked).x;
    if (!require(x > 0.05f,
                "第一个固定步里设的速度没生效（x=" + std::to_string(x) +
                        "）—— 那时候 body 还不存在？")) {
        return false;
    }

    // 力也要能推动东西：30 N 顶 1 kg，扣掉重力净剩 20 m/s²。
    const core::UUID hover = spawnScripted(
            scene, "Hover", workspace.scripts.at("hover"), glm::vec3{ 6.0f, 0.5f, 0.0f });
    addBody(scene, hover, engine::RigidBodyComponent::Type::Dynamic);
    for (int frame = 0; frame < 60; ++frame) {
        world.tick(1.0f / 60.0f);
    }
    if (!require(translationOf(scene, hover).y > 3.0f,
                "每个固定步施力没能把盒子顶起来（y=" +
                        std::to_string(translationOf(scene, hover).y) + "）")) {
        return false;
    }
    return true;
}

// ---- 3. 绑定层的失败路径：没有刚体的实体上，三个调用都返回失败且不崩 ----
bool checkGuardedBindings(Workspace& workspace) {
    engine::World world;
    world.setAssets(&workspace.pipeline.manager());
    auto& scene = world.scene();
    // **刻意不给它刚体。**
    const core::UUID guard = spawnScripted(scene, "Guard", workspace.scripts.at("guard"));

    constexpr int kFrames = 10;
    for (int frame = 0; frame < kFrames; ++frame) {
        world.tick(1.0f / 60.0f);
    }
    const glm::vec3 counts = translationOf(scene, guard);
    if (!require(std::fabs(counts.x - kFrames) < 1e-5f && std::fabs(counts.y - kFrames) < 1e-5f &&
                        std::fabs(counts.z - kFrames) < 1e-5f,
                "没有刚体时控制调用没有全部失败 —— 计数 " + vecText(counts) + "，应该都是 " +
                        std::to_string(kFrames))) {
        return false;
    }
    return true;
}

// ---- 4. 固定回调抛错 → 两种回调一起停 ----
bool checkSharedDisable(Workspace& workspace) {
    engine::World world;
    world.setAssets(&workspace.pipeline.manager());
    auto& scene = world.scene();
    const core::UUID broken = spawnScripted(scene, "Broken", workspace.scripts.at("boom"));

    // 第一帧：固定回调改了 x 之后才抛错，所以 x == 1；on_update 在同一帧还没被禁用之前
    // 就不该再跑了（禁用是在固定回调抛错的那一刻生效的）。
    world.tick(1.0f / 60.0f);
    const glm::vec3 first = translationOf(scene, broken);
    if (!require(std::fabs(first.x - 1.0f) < 1e-5f,
                "抛错前的那次改动没生效 —— 计数 " + vecText(first))) {
        return false;
    }
    if (!require(std::fabs(first.z) < 1e-5f,
                "固定回调抛错之后 on_update 还在跑 —— 计数 " + vecText(first) +
                        "，两种回调该共用一份禁用状态")) {
        return false;
    }

    // 再跑几帧：两个轴都不许再涨。
    for (int frame = 0; frame < 5; ++frame) {
        world.tick(1.0f / 60.0f);
    }
    const glm::vec3 later = translationOf(scene, broken);
    if (!require(std::fabs(later.x - 1.0f) < 1e-5f && std::fabs(later.z) < 1e-5f,
                "抛错的脚本没被禁用 —— 计数 " + vecText(later))) {
        return false;
    }
    return true;
}

// ---- 5. 回调里删实体：自己、别人，遍历都不许坏 ----
bool checkDestroyDuringCallback(Workspace& workspace) {
    engine::World world;
    world.setAssets(&workspace.pipeline.manager());
    auto& scene = world.scene();

    const core::UUID suicide = spawnScripted(scene, "Suicide", workspace.scripts.at("suicide"));
    const core::UUID victim = spawnScripted(scene, "Victim", workspace.scripts.at("cadence"));
    spawnScripted(scene, "Killer", workspace.scripts.at("killer"));
    const core::UUID witness = spawnScripted(scene, "Witness", workspace.scripts.at("cadence"));

    for (int frame = 0; frame < 5; ++frame) {
        world.tick(1.0f / 60.0f);
    }

    if (!require(!scene.findEntity(suicide).isValid(), "在固定回调里自杀的实体还活着")) {
        return false;
    }
    if (!require(!scene.findEntity(victim).isValid(), "被别的脚本删掉的实体还活着")) {
        return false;
    }
    // 旁观者必须一路数完 —— 遍历要是坏了，它会漏掉几次或者干脆崩。
    const glm::vec3 counts = translationOf(scene, witness);
    if (!require(std::fabs(counts.x - 5.0f) < 1e-5f && std::fabs(counts.z - 5.0f) < 1e-5f,
                "同一批里有实体自删 / 被删，旁观者的回调漏了 —— 计数 " + vecText(counts))) {
        return false;
    }
    return true;
}

// ---- 6. 固定步的结果和渲染帧率无关 ----
struct MarchResult {
    float fixed_steps{ 0.0f };
    float height{ 0.0f };
};

// 同一份场景跑同样长的模拟时间，只换渲染帧长。
MarchResult march(Workspace& workspace, float frame_time, int frames) {
    engine::World world;
    world.setAssets(&workspace.pipeline.manager());
    auto& scene = world.scene();
    makeGround(scene);
    const core::UUID counter = spawnScripted(scene, "Counter", workspace.scripts.at("cadence"));
    const core::UUID lifted = spawnScripted(
            scene, "Lifted", workspace.scripts.at("hover"), glm::vec3{ 0.0f, 0.5f, 0.0f });
    addBody(scene, lifted, engine::RigidBodyComponent::Type::Dynamic);

    for (int frame = 0; frame < frames; ++frame) {
        world.tick(frame_time);
    }
    return MarchResult{ translationOf(scene, counter).x, translationOf(scene, lifted).y };
}

bool checkFrameRateIndependence(Workspace& workspace) {
    // 1.005 秒。刻意不取整秒：整秒正好是 60 个固定步的边界，浮点误差会让断言在 59 和 60
    // 之间摇摆。多出来的 5 毫秒是两边各 1/3 个固定步的余量。
    constexpr float kTotal = 1.005f;
    const MarchResult at30 = march(workspace, kTotal / 30.0f, 30);
    const MarchResult at60 = march(workspace, kTotal / 60.0f, 60);
    const MarchResult at120 = march(workspace, kTotal / 120.0f, 120);

    const auto steps = [](const MarchResult& result) { return std::to_string(result.fixed_steps); };
    if (!require(std::fabs(at30.fixed_steps - 60.0f) < 1e-5f &&
                        std::fabs(at60.fixed_steps - 60.0f) < 1e-5f &&
                        std::fabs(at120.fixed_steps - 60.0f) < 1e-5f,
                "一秒里的固定步数跟着帧率变了：30Hz " + steps(at30) + "，60Hz " + steps(at60) +
                        "，120Hz " + steps(at120) + "，都该是 60")) {
        return false;
    }
    const auto height = [](const MarchResult& result) { return std::to_string(result.height); };
    if (!require(std::fabs(at30.height - at60.height) < 1e-3f &&
                        std::fabs(at120.height - at60.height) < 1e-3f,
                "同样的固定步数跑出了不同的物理结果：30Hz y=" + height(at30) + "，60Hz y=" +
                        height(at60) + "，120Hz y=" + height(at120))) {
        return false;
    }
    return true;
}

// ---- 7. 只跑一帧就重开会话：VM 必须是新的 ----
//
// 光靠帧号回退认不出这一种（两次都是 frameIndex == 0），所以 World::resetClock 显式通知（D5）。
// 反向验证：把那两句 requestSessionReset 去掉，on_create 不会再跑第二次，y 停在 1。
bool checkSessionReset(Workspace& workspace) {
    engine::World world;
    world.setAssets(&workspace.pipeline.manager());
    auto& scene = world.scene();
    const core::UUID counter = spawnScripted(scene, "Counter", workspace.scripts.at("cadence"));

    // **只跑一帧**，然后「Stop → Play」。
    world.tick(1.0f / 60.0f);
    world.resetClock();
    world.tick(1.0f / 60.0f);

    const glm::vec3 counts = translationOf(scene, counter);
    if (!require(std::fabs(counts.y - 2.0f) < 1e-5f,
                "重开会话之后 on_create 没有再跑一次 —— 旧的 Lua VM 被继承下来了。计数 " +
                        vecText(counts) + "，y 应该是 2")) {
        return false;
    }
    return true;
}

int run() {
    Workspace workspace{ TemporaryDirectory{ std::filesystem::temp_directory_path() /
            ("ArtiScriptPhysics-" + core::UUID::generate().toString()) } };
    if (!workspace.open()) {
        return 1;
    }

    if (!checkCadence(workspace)) {
        return 1;
    }
    if (!checkSameStepEffect(workspace)) {
        return 1;
    }
    if (!checkGuardedBindings(workspace)) {
        return 1;
    }
    if (!checkSharedDisable(workspace)) {
        return 1;
    }
    if (!checkDestroyDuringCallback(workspace)) {
        return 1;
    }
    if (!checkFrameRateIndependence(workspace)) {
        return 1;
    }
    if (!checkSessionReset(workspace)) {
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    arti::core::Logger::init();
    arti::core::TaskSystem::init();
    const int result = run();
    arti::core::TaskSystem::shutdown();
    arti::core::Logger::shutdown();
    return result;
}
