#include "script/detail/script_bindings.h"

#include "asset/prefab_asset.h"
#include "asset/script_asset.h"
#include "engine_log.h"
#include "physics/physics_system.h"
#include "scene/components.h"
#include "scene/prefab_instantiation.h"

#include "artichoco/asset/asset_manager.h"
#include "artichoco/core/io/input.h"
#include "artichoco/core/io/key_codes.h"
#include "artichoco/core/uuid.h"
#include "artichoco/scene/components.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace arti::engine::detail {
namespace {

using namespace arti;

struct ScriptEntity {
    core::UUID id;
    scene::Scene* scene{ nullptr };

    scene::Entity resolve() const {
        if (scene == nullptr || !id.isValid()) {
            return {};
        }
        return scene->findEntity(id);
    }
};

sol::table vec3ToTable(sol::state_view lua, const glm::vec3& value) {
    sol::table table = lua.create_table(0, 3);
    table["x"] = value.x;
    table["y"] = value.y;
    table["z"] = value.z;
    return table;
}

glm::vec3 tableToVec3(const sol::table& table, const glm::vec3& fallback) {
    glm::vec3 value = fallback;
    if (const sol::optional<float> x = table["x"]; x) {
        value.x = *x;
    }
    if (const sol::optional<float> y = table["y"]; y) {
        value.y = *y;
    }
    if (const sol::optional<float> z = table["z"]; z) {
        value.z = *z;
    }
    return value;
}

std::optional<core::KeyCode> parseKey(std::string_view name) {
    if (name.size() == 1) {
        const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        if (letter >= 'A' && letter <= 'Z') {
            return static_cast<core::KeyCode>(letter);
        }
    }
    if (name == "Space" || name == "space") {
        return core::KeyCode::Space;
    }
    if (name == "Escape" || name == "escape" || name == "Esc" || name == "esc") {
        return core::KeyCode::Escape;
    }
    if (name == "Shift" || name == "shift") {
        return core::KeyCode::LeftShift;
    }
    if (name == "Ctrl" || name == "ctrl" || name == "Control" || name == "control") {
        return core::KeyCode::LeftControl;
    }
    return std::nullopt;
}

bool isKeyPressed(std::string_view name, std::unordered_set<std::string>& warned) {
    const auto key = parseKey(name);
    if (!key) {
        const std::string owned{ name };
        if (warned.insert(owned).second) {
            getLogChannel().warn("arti.input.is_key_pressed: unknown key '{}'", owned);
        }
        return false;
    }
    if (*key == core::KeyCode::LeftShift) {
        return core::Input::isKeyPressed(core::KeyCode::LeftShift) ||
                core::Input::isKeyPressed(core::KeyCode::RightShift);
    }
    if (*key == core::KeyCode::LeftControl) {
        return core::Input::isKeyPressed(core::KeyCode::LeftControl) ||
                core::Input::isKeyPressed(core::KeyCode::RightControl);
    }
    return core::Input::isKeyPressed(*key);
}

// 一条刚体控制调用失败了，说清楚是哪一种失败 —— 「返回 false」本身没有信息量，而这三种
// 原因的修法完全不同。**按 (操作, 实体) 去重报一次**：脚本里这类错误是静态的（API 用错了），
// 每帧一条会把日志刷爆，而刷爆的日志等于没有日志。
void warnPhysicsFailure(std::unordered_set<std::string>& warned, const char* op,
        const ScriptEntity& entity, bool requires_dynamic) {
    if (!warned.insert(std::string{ op } + ':' + entity.id.toString()).second) {
        return;
    }

    const std::string id = entity.id.toString();
    const auto handle = entity.resolve();
    if (!handle.isValid()) {
        getLogChannel().warn("arti.physics.{}: entity {} does not exist", op, id);
        return;
    }
    if (!handle.hasComponent<RigidBodyComponent>() || !handle.hasComponent<ColliderComponent>()) {
        getLogChannel().warn(
                "arti.physics.{}: entity {} needs both a RigidBody and a Collider to have a body",
                op, id);
        return;
    }
    if (requires_dynamic &&
            handle.getComponent<RigidBodyComponent>().type !=
                    RigidBodyComponent::Type::Dynamic) {
        getLogChannel().warn("arti.physics.{}: entity {} is not a Dynamic body -- drive Static / "
                            "Kinematic bodies by writing entity.translation instead",
                op, id);
        return;
    }
    // 组件都在、类型也对，那就是这个实体没进模拟：带父级、缩放过、或者还没跑过一个固定步。
    getLogChannel().warn("arti.physics.{}: entity {} has no rigid body in the simulation (skipped "
                        "because of a parent or a non-unit scale?)",
            op, id);
}

// 三个「吃一个向量、返回成功与否」的接口共用的外壳。
template<typename Operation>
bool applyVector(scene::Scene& scene, std::unordered_set<std::string>& warned, const char* op,
        const ScriptEntity& entity, const sol::table& table, Operation operation) {
    if (!scene.hasSystem<PhysicsSystem>()) {
        return false;
    }
    // 缺的分量当 0。非有限的值由 PhysicsSystem 挡下来返回 false —— 那会踩 Box3D 的断言。
    const glm::vec3 value = tableToVec3(table, glm::vec3{ 0.0f });
    if (operation(scene.getSystem<PhysicsSystem>(), entity.id, value)) {
        return true;
    }
    warnPhysicsFailure(warned, op, entity, true);
    return false;
}

} // namespace

void bindScriptApi(sol::state& lua, scene::Scene& scene, arti::asset::AssetManager* assets,
        std::unordered_set<std::string>& warned_keys) {
    lua.new_usertype<ScriptEntity>("Entity", sol::no_constructor,
            "uuid",
            sol::readonly_property([](const ScriptEntity& entity) { return entity.id.toString(); }),
            "name",
            sol::property(
                    [](const ScriptEntity& entity) -> std::string {
                        const auto handle = entity.resolve();
                        if (!handle.isValid()) {
                            return {};
                        }
                        return handle.getComponent<scene::TagComponent>().tag;
                    },
                    [](ScriptEntity& entity, std::string_view name) {
                        auto handle = entity.resolve();
                        if (handle.isValid()) {
                            handle.getComponent<scene::TagComponent>().tag = std::string{ name };
                        }
                    }),
            "translation",
            sol::property(
                    [&lua](const ScriptEntity& entity) {
                        const auto handle = entity.resolve();
                        const glm::vec3 value = handle.isValid()
                                ? handle.getComponent<scene::TransformComponent>().translation
                                : glm::vec3{ 0.0f };
                        return vec3ToTable(lua, value);
                    },
                    [](ScriptEntity& entity, const sol::table& table) {
                        auto handle = entity.resolve();
                        if (!handle.isValid()) {
                            return;
                        }
                        auto& transform = handle.getComponent<scene::TransformComponent>();
                        transform.translation = tableToVec3(table, transform.translation);
                    }),
            "rotation_euler",
            sol::property(
                    [&lua](const ScriptEntity& entity) {
                        const auto handle = entity.resolve();
                        glm::vec3 degrees{ 0.0f };
                        if (handle.isValid()) {
                            degrees = glm::degrees(glm::eulerAngles(
                                    handle.getComponent<scene::TransformComponent>().rotation));
                        }
                        return vec3ToTable(lua, degrees);
                    },
                    [](ScriptEntity& entity, const sol::table& table) {
                        auto handle = entity.resolve();
                        if (!handle.isValid()) {
                            return;
                        }
                        auto& transform = handle.getComponent<scene::TransformComponent>();
                        const glm::vec3 previous =
                                glm::degrees(glm::eulerAngles(transform.rotation));
                        const glm::vec3 degrees = tableToVec3(table, previous);
                        transform.rotation = glm::quat{ glm::radians(degrees) };
                    }),
            "scale",
            sol::property(
                    [&lua](const ScriptEntity& entity) {
                        const auto handle = entity.resolve();
                        const glm::vec3 value = handle.isValid()
                                ? handle.getComponent<scene::TransformComponent>().scale
                                : glm::vec3{ 1.0f };
                        return vec3ToTable(lua, value);
                    },
                    [](ScriptEntity& entity, const sol::table& table) {
                        auto handle = entity.resolve();
                        if (!handle.isValid()) {
                            return;
                        }
                        auto& transform = handle.getComponent<scene::TransformComponent>();
                        transform.scale = tableToVec3(table, transform.scale);
                    }),
            "destroy", [](ScriptEntity& entity) {
                auto handle = entity.resolve();
                if (handle.isValid()) {
                    entity.scene->destroyEntity(handle);
                }
            });

    sol::table arti = lua.create_named_table("arti");

    sol::table input = lua.create_table();
    input.set_function("is_key_pressed",
            [&warned_keys](std::string_view name) { return isKeyPressed(name, warned_keys); });
    arti["input"] = input;

    sol::table physics = lua.create_table();
    physics.set_function("raycast", [&lua, &scene](const sol::table& origin_table,
                                            const sol::table& translation_table) -> sol::object {
        if (!scene.hasSystem<PhysicsSystem>()) {
            return sol::lua_nil;
        }
        const glm::vec3 origin = tableToVec3(origin_table, glm::vec3{ 0.0f });
        const glm::vec3 translation = tableToVec3(translation_table, glm::vec3{ 0.0f });
        const auto hit = scene.getSystem<PhysicsSystem>().raycast(origin, translation);
        if (!hit) {
            return sol::lua_nil;
        }
        sol::table result = lua.create_table(0, 4);
        result["uuid"] = hit->entity.toString();
        result["point"] = vec3ToTable(lua, hit->point);
        result["normal"] = vec3ToTable(lua, hit->normal);
        result["fraction"] = hit->fraction;
        return result;
    });
    // ---- 刚体控制（D3）。都按 entity 收参，不是 Box3D 的 body id ----
    //
    // 速度 / 力 / 冲量**只对 Dynamic 有意义**：Kinematic 要动就写 entity.translation，那是
    // transform 所有权规则（Scene.md 3.1.1），不是这里悄悄不生效。失败返回 false 并报一次原因。
    physics.set_function("get_linear_velocity",
            [&lua, &scene](const ScriptEntity& entity) -> sol::object {
                if (!scene.hasSystem<PhysicsSystem>()) {
                    return sol::lua_nil;
                }
                const auto velocity =
                        scene.getSystem<PhysicsSystem>().linearVelocity(entity.id);
                if (!velocity) {
                    return sol::lua_nil;
                }
                return vec3ToTable(lua, *velocity);
            });
    physics.set_function("set_linear_velocity",
            [&scene, &warned_keys](const ScriptEntity& entity, const sol::table& velocity) {
                return applyVector(scene, warned_keys, "set_linear_velocity", entity, velocity,
                        [](PhysicsSystem& system, core::UUID id, const glm::vec3& value) {
                            return system.setLinearVelocity(id, value);
                        });
            });
    physics.set_function("apply_force",
            [&scene, &warned_keys](const ScriptEntity& entity, const sol::table& force) {
                return applyVector(scene, warned_keys, "apply_force", entity, force,
                        [](PhysicsSystem& system, core::UUID id, const glm::vec3& value) {
                            return system.applyForce(id, value);
                        });
            });
    physics.set_function("apply_impulse",
            [&scene, &warned_keys](const ScriptEntity& entity, const sol::table& impulse) {
                return applyVector(scene, warned_keys, "apply_impulse", entity, impulse,
                        [](PhysicsSystem& system, core::UUID id, const glm::vec3& value) {
                            return system.applyImpulse(id, value);
                        });
            });
    // teleport 对三种类型都成立（它同时改场景和物理），所以不筛 Dynamic。
    physics.set_function("teleport",
            [&scene, &warned_keys](const ScriptEntity& entity, const sol::table& position) {
                if (!scene.hasSystem<PhysicsSystem>()) {
                    return false;
                }
                const auto handle = entity.resolve();
                const glm::vec3 fallback = handle.isValid()
                        ? handle.getComponent<scene::TransformComponent>().translation
                        : glm::vec3{ 0.0f };
                // 缺的分量保持原值：teleport({ y = 3 }) 是「抬到 3 米高」，不是「挪到 x=0」。
                const glm::vec3 target = tableToVec3(position, fallback);
                if (scene.getSystem<PhysicsSystem>().teleport(scene, entity.id, target)) {
                    return true;
                }
                warnPhysicsFailure(warned_keys, "teleport", entity, false);
                return false;
            });
    arti["physics"] = physics;

    sol::table scene_table = lua.create_table();
    scene_table.set_function("find_by_tag", [&lua, &scene](std::string_view tag) -> sol::object {
        auto entity = scene.findEntityByTag(tag);
        if (!entity.isValid()) {
            return sol::lua_nil;
        }
        return sol::make_object(lua, ScriptEntity{ entity.getUUID(), &scene });
    });
    scene_table.set_function("spawn_prefab",
            [&lua, &scene, assets](std::string_view uuid_text) -> sol::object {
                if (assets == nullptr) {
                    return sol::lua_nil;
                }
                const auto parsed = core::UUID::fromString(uuid_text);
                if (!parsed) {
                    return sol::lua_nil;
                }
                const auto prefab = assets->load<asset::PrefabAsset>(*parsed);
                if (!prefab) {
                    return sol::lua_nil;
                }
                const auto root = instantiatePrefab(scene, *prefab);
                if (!root.isValid()) {
                    return sol::lua_nil;
                }
                return sol::make_object(lua, ScriptEntity{ root.getUUID(), &scene });
            });
    arti["scene"] = scene_table;

    sol::table log = lua.create_table();
    log.set_function("info", [](std::string_view message) { getLogChannel().info("{}", message); });
    log.set_function("warn", [](std::string_view message) { getLogChannel().warn("{}", message); });
    log.set_function("error", [](std::string_view message) { getLogChannel().error("{}", message); });
    arti["log"] = log;
}

sol::object makeEntityObject(sol::state& lua, scene::Scene& scene, core::UUID id) {
    return sol::make_object(lua, ScriptEntity{ id, &scene });
}

} // namespace arti::engine::detail
