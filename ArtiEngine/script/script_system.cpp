#include "script/script_system.h"

#include "asset/script_asset.h"
#include "engine_log.h"
#include "scene/components.h"
#include "script/detail/script_bindings.h"

#include "artichoco/asset/asset_manager.h"
#include "artichoco/core/uuid.h"
#include "artichoco/scene/components.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <sol/sol.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace arti::engine {
namespace {

bool invoke(sol::environment& environment, const char* name, const sol::object& entity,
        float dt, bool with_dt, const core::UUID& id) {
    const sol::protected_function fn = environment[name];
    if (!fn.valid()) {
        return true;
    }
    const sol::protected_function_result result = with_dt ? fn(entity, dt) : fn(entity);
    if (result.valid()) {
        return true;
    }
    const sol::error error = result;
    getLogChannel().error("Script {} on entity {} failed: {}", name, id.toString(), error.what());
    return false;
}

} // namespace

struct ScriptSystem::Impl {
    struct Instance {
        sol::environment environment;
        bool disabled{ false };
        bool created{ false };
    };

    sol::state lua;
    std::unordered_map<core::UUID, Instance> instances;
    std::unordered_set<std::string> warned_keys;
    arti::asset::AssetManager* assets{ nullptr };
    std::uint64_t last_frame_index{ 0 };
    bool bound{ false };
    bool warned_missing_assets{ false };

    void resetState() {
        instances.clear();
        warned_keys.clear();
        bound = false;
        lua = sol::state{};
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
    }

    void ensureBound(scene::Scene& scene) {
        if (bound) {
            return;
        }
        detail::bindScriptApi(lua, scene, assets, warned_keys);
        bound = true;
    }

    Instance* createInstance(scene::Scene& scene, core::UUID id, const ScriptComponent& component) {
        if (assets == nullptr) {
            if (!warned_missing_assets) {
                getLogChannel().warn("ScriptSystem has no AssetManager; scripts will not run. "
                                     "Call World::setAssets() after opening the workspace.");
                warned_missing_assets = true;
            }
            return nullptr;
        }
        if (!component.script.isValid()) {
            return nullptr;
        }

        const auto asset = assets->load<asset::ScriptAsset>(component.script);
        if (!asset) {
            getLogChannel().error("Failed to load script asset {} for entity {}",
                    component.script.id().toString(), id.toString());
            Instance disabled;
            disabled.environment = sol::environment{ lua, sol::create, lua.globals() };
            disabled.disabled = true;
            disabled.created = true;
            auto [it, inserted] = instances.emplace(id, std::move(disabled));
            (void)inserted;
            return &it->second;
        }

        Instance instance;
        instance.environment = sol::environment{ lua, sol::create, lua.globals() };
        const auto loaded = lua.safe_script(asset->source(), instance.environment,
                sol::script_pass_on_error);
        if (!loaded.valid()) {
            const sol::error error = loaded;
            getLogChannel().error("Failed to compile script {} for entity {}: {}",
                    component.script.id().toString(), id.toString(), error.what());
            instance.disabled = true;
            instance.created = true;
            auto [it, inserted] = instances.emplace(id, std::move(instance));
            (void)inserted;
            return &it->second;
        }

        auto [it, inserted] = instances.emplace(id, std::move(instance));
        (void)inserted;
        auto& live = it->second;
        const sol::object entity = detail::makeEntityObject(lua, scene, id);
        if (!invoke(live.environment, "on_create", entity, 0.0f, false, id)) {
            live.disabled = true;
        }
        live.created = true;
        return &live;
    }

    void sync(scene::Scene& scene, float dt) {
        ensureBound(scene);

        std::unordered_set<core::UUID> live;
        for (auto [handle, id, script]:
                scene.view<scene::IDComponent, ScriptComponent>().each()) {
            (void)handle;
            live.insert(id.id);
            auto found = instances.find(id.id);
            if (found == instances.end()) {
                createInstance(scene, id.id, script);
                found = instances.find(id.id);
            }
            if (found == instances.end() || found->second.disabled) {
                continue;
            }
            const sol::object entity = detail::makeEntityObject(lua, scene, id.id);
            if (!invoke(found->second.environment, "on_update", entity, dt, true, id.id)) {
                found->second.disabled = true;
            }
        }

        std::vector<core::UUID> gone;
        for (auto& [id, instance]: instances) {
            if (live.contains(id)) {
                continue;
            }
            if (!instance.disabled && instance.created) {
                const sol::object entity = detail::makeEntityObject(lua, scene, id);
                invoke(instance.environment, "on_destroy", entity, 0.0f, false, id);
            }
            gone.push_back(id);
        }
        for (const auto id: gone) {
            instances.erase(id);
        }
    }
};

ScriptSystem::ScriptSystem() : m_impl(std::make_unique<Impl>()) {
    m_impl->resetState();
}

ScriptSystem::~ScriptSystem() = default;

void ScriptSystem::setAssets(arti::asset::AssetManager* assets) noexcept {
    m_impl->assets = assets;
    m_impl->warned_missing_assets = false;
}

void ScriptSystem::onUpdate(scene::Scene& scene, const scene::UpdateContext& context) {
    const bool restarted = context.frameIndex < m_impl->last_frame_index;
    m_impl->last_frame_index = context.frameIndex;
    if (restarted) {
        m_impl->resetState();
    }
    m_impl->sync(scene, context.deltaTime.getSeconds());
}

} // namespace arti::engine
