// 脚本端到端冒烟：真的从 Assets/ 导入一个 .lua、挂在实体上、tick 一次，看 transform 变了没有。
//
// 放在 Tools 而不是 ArtiEngine：它需要 AssetPipeline（importer 只注册在那儿）**和** World，
// 而 ArtiEngine 的测试目标链不到 ArtiTools::Asset。
//
// 两条断言是这个任务真正的防线：
//   1. 脚本能改场景（不然整层白做）
//   2. 脚本里的 error 不许穿过 World::tick，而且那个实例必须被禁用 —— 否则一个手误的脚本
//      会每帧刷屏、甚至把编辑器带走（Stop 都按不到）。

#include "asset_tools/asset_pipeline.h"

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
        std::cerr << "script_runtime_smoke: " << message << '\n';
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

// 每帧把 x 加 1。用整数步进而不是 dt * speed：断言就不用挑容差。
constexpr std::string_view kNudgeSource = R"(
function on_update(entity, dt)
    local t = entity.translation
    t.x = t.x + 1
    entity.translation = t
end
)";

// 先改一下再抛。这样「抛了之后有没有被禁用」可以直接用 x 的值判断：
// 禁用了 x 停在 1，没禁用会一直涨。
constexpr std::string_view kBoomSource = R"(
function on_update(entity, dt)
    local t = entity.translation
    t.x = t.x + 1
    entity.translation = t
    error("boom from the smoke test")
end
)";

float translationX(scene::Scene& scene, core::UUID id) {
    return scene.findEntity(id).getComponent<scene::TransformComponent>().translation.x;
}

int run() {
    TemporaryDirectory temp{ std::filesystem::temp_directory_path() /
        ("ArtiScriptRuntime-" + core::UUID::generate().toString()) };
    const auto assets = temp.path / "Assets";
    const auto artifacts = temp.path / "Artifacts";
    std::error_code error;
    std::filesystem::create_directories(assets, error);
    std::filesystem::create_directories(artifacts, error);
    if (!require(!error, "failed to create the temporary workspace")) {
        return 1;
    }

    if (!require(writeText(assets / "Scripts" / "nudge.lua", kNudgeSource),
                "failed to write nudge.lua") ||
            !require(writeText(assets / "Scripts" / "boom.lua", kBoomSource),
                    "failed to write boom.lua")) {
        return 1;
    }

    tools::asset::AssetPipeline pipeline;
    if (!require(pipeline.open(assets, artifacts), "failed to open the asset pipeline")) {
        return 1;
    }
    if (!require(pipeline.reconcile().succeeded(), "reconcile failed")) {
        return 1;
    }

    const auto nudge = pipeline.sourceAssets("Scripts/nudge.lua");
    const auto boom = pipeline.sourceAssets("Scripts/boom.lua");
    if (!require(nudge.assets.size() == 1 && boom.assets.size() == 1,
                "the lua sources did not each produce one asset")) {
        return 1;
    }

    engine::World world;
    world.setAssets(&pipeline.manager());
    auto& scene = world.scene();

    auto mover = scene.createEntity("Mover");
    mover.addComponent<engine::ScriptComponent>().script =
            arti::asset::AssetHandle<engine::asset::ScriptAsset>{ nudge.assets.front().handle };
    const core::UUID mover_id = mover.getUUID();

    auto broken = scene.createEntity("Broken");
    broken.addComponent<engine::ScriptComponent>().script =
            arti::asset::AssetHandle<engine::asset::ScriptAsset>{ boom.assets.front().handle };
    const core::UUID broken_id = broken.getUUID();

    // ---- 一次 tick：脚本真的改了场景 ----
    world.tick(1.0f / 60.0f);
    if (!require(std::fabs(translationX(scene, mover_id) - 1.0f) < 1e-5f,
                "on_update did not move the entity (x=" +
                        std::to_string(translationX(scene, mover_id)) + ")")) {
        return 1;
    }

    // 抛错的那个：这一帧的改动生效了（error 在改完之后），但实例必须已经被禁用。
    if (!require(std::fabs(translationX(scene, broken_id) - 1.0f) < 1e-5f,
                "the failing script did not apply its pre-error change")) {
        return 1;
    }

    // ---- 再 tick 两次：好的继续动，坏的停住 ----
    world.tick(1.0f / 60.0f);
    world.tick(1.0f / 60.0f);
    if (!require(std::fabs(translationX(scene, mover_id) - 3.0f) < 1e-5f,
                "on_update stopped running (x=" + std::to_string(translationX(scene, mover_id)) +
                        ", expected 3)")) {
        return 1;
    }
    if (!require(std::fabs(translationX(scene, broken_id) - 1.0f) < 1e-5f,
                "the failing script was not disabled after throwing (x=" +
                        std::to_string(translationX(scene, broken_id)) + ", expected 1)")) {
        return 1;
    }

    // ---- 删掉实体不该崩，也不该继续跑 ----
    scene.destroyEntity(scene.findEntity(mover_id));
    world.tick(1.0f / 60.0f);

    // ---- 没有 AssetManager 时跳过而不是崩 ----
    {
        engine::World bare;
        auto entity = bare.scene().createEntity("NoAssets");
        entity.addComponent<engine::ScriptComponent>().script =
                arti::asset::AssetHandle<engine::asset::ScriptAsset>{
                    nudge.assets.front().handle
                };
        bare.tick(1.0f / 60.0f);
        if (!require(std::fabs(entity.getComponent<scene::TransformComponent>().translation.x) <
                            1e-5f,
                    "a World without assets still ran the script")) {
            return 1;
        }
    }

    // ---- 无效 handle 也只是跳过 ----
    {
        auto entity = scene.createEntity("BadHandle");
        entity.addComponent<engine::ScriptComponent>();
        world.tick(1.0f / 60.0f);
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
