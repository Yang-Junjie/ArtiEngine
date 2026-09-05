// 仓库里那几份**示例**资产的自动验收：`projects/Assets/Scripts/*.lua` 和引用它们的示例场景。
//
// 为什么值得一个测试：示例是给人看的，而人只会在 GUI 里看。于是它们最容易悄悄烂掉 ——
// 脚本里一个语法错误、场景里一个过期的 UUID、或者给会动的平台加了个缩放（物理就跳过它了），
// 三种都不会让任何别的测试变红，只会在下一次有人打开编辑器时表现成「怎么不动了」。
//
// 这里能自动验的三件事：
//   1. 每份 .lua 在沙箱库（base / math / string / table）下真的能编译并跑完顶层
//   2. 场景反序列化得出来，而且脚本 UUID 和手写的 .meta 里那个对得上
//   3. 凡是带 RigidBody + Collider 的实体，都没有父级、缩放都是 1 —— 不然物理会跳过它
//
// **不能**自动验的：按键真的推得动东西、画面上看起来对。那两条只能人在窗口里做，
// 任务文档里单独列着。

#include "runtime/world.h"
#include "scene/components.h"

#include "artichoco/core/log.h"
#include "artichoco/core/uuid.h"
#include "artichoco/scene/components.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <sol/sol.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using namespace arti;

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "example_assets_smoke: " << message << '\n';
    }
    return condition;
}

std::filesystem::path projectDir() { return std::filesystem::path{ ARTIENGINE_PROJECT_DIR }; }

std::string readText(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// 引擎绑的那套 arti.* 的替身。示例脚本的顶层不该调它们，但**将来可能会**，而在这里因为
// 「没有 arti」而红掉是个假失败 —— 真正跑的时候引擎是先绑好再执行脚本的。
constexpr std::string_view kStubApi = R"(
arti = {
    log = { info = function() end, warn = function() end, error = function() end },
    input = { is_key_pressed = function() return false end },
    physics = {
        raycast = function() return nil end,
        get_linear_velocity = function() return nil end,
        set_linear_velocity = function() return false end,
        apply_force = function() return false end,
        apply_impulse = function() return false end,
        teleport = function() return false end,
    },
    scene = { find_by_tag = function() return nil end, spawn_prefab = function() return nil end },
}
)";

// 一份示例脚本：文件名、手写 .meta 里钉住的 UUID、以及它必须定义出来的回调。
struct ExampleScript {
    std::string_view file;
    std::string_view handle;
    std::string_view required_callback;
};

constexpr ExampleScript kScripts[] = {
    { "wasd_move.lua", "5c81970000000001", "on_update" },
    { "physics_move.lua", "5c81970000000002", "on_fixed_update" },
    { "platform_lift.lua", "5c81970000000003", "on_fixed_update" },
};

bool checkScripts() {
    for (const auto& script: kScripts) {
        const auto source_path = projectDir() / "Assets" / "Scripts" / script.file;
        const std::string source = readText(source_path);
        if (!require(!source.empty(),
                    "读不到示例脚本 " + source_path.string() + "（示例被删了还是路径变了？）")) {
            return false;
        }

        // 手写的 .meta 是「示例场景能引用一个固定 UUID」的唯一办法（reconcile 只会因为
        // artifact 缺失而重导，身份原样保留）。所以这两边必须对得上。
        const std::string meta = readText(source_path.string() + ".meta");
        if (!require(meta.find(std::string{ "Handle: " } + std::string{ script.handle }) !=
                            std::string::npos,
                    std::string{ script.file } + ".meta 里的 Handle 不是 " +
                            std::string{ script.handle } + " —— 场景里的引用会指空")) {
            return false;
        }

        // 只开沙箱那四个库，和 ScriptSystem 一致（没有 io / os / require）。
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
        if (!require(lua.safe_script(kStubApi, sol::script_pass_on_error).valid(),
                    "arti 替身自己就没跑起来")) {
            return false;
        }
        const auto loaded = lua.safe_script(source, sol::script_pass_on_error);
        if (!loaded.valid()) {
            const sol::error error = loaded;
            require(false,
                    std::string{ script.file } + " 编译 / 执行失败：" + error.what());
            return false;
        }
        const sol::protected_function callback = lua[script.required_callback];
        if (!require(callback.valid(),
                    std::string{ script.file } + " 没有定义 " +
                            std::string{ script.required_callback })) {
            return false;
        }
    }
    return true;
}

// 一个实体应该长成什么样。
struct ExpectedEntity {
    std::string_view tag;
    engine::RigidBodyComponent::Type type;
    std::string_view script;  // 空串表示这个实体不该挂脚本
};

constexpr ExpectedEntity kExpected[] = {
    { "Ground Body", engine::RigidBodyComponent::Type::Static, "" },
    { "Player", engine::RigidBodyComponent::Type::Dynamic, "5c81970000000002" },
    { "Platform Left", engine::RigidBodyComponent::Type::Kinematic, "5c81970000000003" },
    { "Platform Middle", engine::RigidBodyComponent::Type::Kinematic, "5c81970000000003" },
    { "Platform Right", engine::RigidBodyComponent::Type::Kinematic, "5c81970000000003" },
    { "Crate A", engine::RigidBodyComponent::Type::Dynamic, "" },
    { "Crate B", engine::RigidBodyComponent::Type::Dynamic, "" },
};

bool checkScene() {
    const auto path = projectDir() / "Assets" / "Scenes" / "physics_move_test.artiscene";
    engine::World world;
    if (!require(world.loadScene(path), "示例场景读不进来：" + path.string())) {
        return false;
    }
    auto& scene = world.scene();

    for (const auto& expected: kExpected) {
        auto entity = scene.findEntityByTag(expected.tag);
        if (!require(entity.isValid(),
                    "示例场景里找不到 '" + std::string{ expected.tag } + "'")) {
            return false;
        }
        if (!require(entity.hasComponent<engine::RigidBodyComponent>() &&
                            entity.hasComponent<engine::ColliderComponent>(),
                    std::string{ expected.tag } + " 缺 RigidBody 或 Collider —— 物理会跳过它")) {
            return false;
        }
        if (!require(entity.getComponent<engine::RigidBodyComponent>().type == expected.type,
                    std::string{ expected.tag } + " 的刚体类型不对")) {
            return false;
        }
        if (expected.script.empty()) {
            continue;
        }
        if (!require(entity.hasComponent<engine::ScriptComponent>(),
                    std::string{ expected.tag } + " 上没有 Script 组件")) {
            return false;
        }
        const auto handle = entity.getComponent<engine::ScriptComponent>().script.id();
        if (!require(handle.toString() == expected.script,
                    std::string{ expected.tag } + " 引用的脚本是 " + handle.toString() +
                            "，应该是 " + std::string{ expected.script } +
                            " —— 手写 .meta 里的 UUID 和场景对不上了")) {
            return false;
        }
    }

    // 凡是要进物理的实体，都不许有父级、缩放必须是 1 —— 否则建世界时会被 warn 掉，
    // 症状是「视觉动了、挡人的没动」。这一条是写示例时最容易踩的那个坑。
    for (auto [handle, id, tag, transform, parent, body, collider]:
            scene.view<scene::IDComponent, scene::TagComponent, scene::TransformComponent,
                            scene::ParentComponent, engine::RigidBodyComponent,
                            engine::ColliderComponent>()
                    .each()) {
        (void)handle;
        (void)id;
        (void)body;
        (void)collider;
        if (!require(!parent.parent_id.isValid(),
                    "'" + tag.tag + "' 带着父级，物理会跳过它")) {
            return false;
        }
        const bool unit_scale = std::fabs(transform.scale.x - 1.0f) < 1e-3f &&
                std::fabs(transform.scale.y - 1.0f) < 1e-3f &&
                std::fabs(transform.scale.z - 1.0f) < 1e-3f;
        if (!require(unit_scale, "'" + tag.tag + "' 的缩放不是 1，物理会跳过它")) {
            return false;
        }
    }
    return true;
}

int run() {
    if (!checkScripts()) {
        return 1;
    }
    if (!checkScene()) {
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    arti::core::Logger::init();
    const int result = run();
    arti::core::Logger::shutdown();
    return result;
}
