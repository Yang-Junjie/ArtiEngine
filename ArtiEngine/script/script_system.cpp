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

// 哪一种时钟上的回调。两种共用一个实例、一份 disabled 状态（D1）。
enum class Callback : std::uint8_t { FixedUpdate, Update };

const char* callbackName(Callback which) {
    return which == Callback::FixedUpdate ? "on_fixed_update" : "on_update";
}

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
    // 派发前的实体身份快照，和收尾时的存活集合。放成员而不是局部变量：一帧里要用好几次，
    // 不想每次都分配。**只存 UUID 和资产 handle** —— 组件引用一个都不跨回调保留。
    std::vector<std::pair<core::UUID, arti::asset::AssetHandle<asset::ScriptAsset>>> pending;
    std::unordered_set<core::UUID> live;
    std::vector<core::UUID> gone;
    arti::asset::AssetManager* assets{ nullptr };
    std::uint64_t last_frame_index{ 0 };
    bool bound{ false };
    bool warned_missing_assets{ false };
    bool session_reset_requested{ false };

    void resetState() {
        // 先丢实例：每个 sol::environment 都引用着下面那个 state，顺序反了就是先拆房再搬家。
        instances.clear();
        warned_keys.clear();
        bound = false;
        session_reset_requested = false;
        lua = sol::state{};
        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table);
    }

    // 会话边界。两种回调都会先过这里，所以一帧里最多重建一次（帧号在一帧内不变）。
    void ensureSession(std::uint64_t frame_index) {
        if (frame_index < last_frame_index) {
            session_reset_requested = true;
        }
        last_frame_index = frame_index;
        if (session_reset_requested) {
            resetState();
        }
    }

    void ensureBound(scene::Scene& scene) {
        if (bound) {
            return;
        }
        detail::bindScriptApi(lua, scene, assets, warned_keys);
        bound = true;
    }

    Instance* createInstance(scene::Scene& scene, core::UUID id,
            const arti::asset::AssetHandle<asset::ScriptAsset>& script) {
        if (assets == nullptr) {
            if (!warned_missing_assets) {
                getLogChannel().warn("ScriptSystem has no AssetManager; scripts will not run. "
                                     "Call World::setAssets() after opening the workspace.");
                warned_missing_assets = true;
            }
            return nullptr;
        }
        if (!script.isValid()) {
            return nullptr;
        }

        const auto asset = assets->load<asset::ScriptAsset>(script);
        if (!asset) {
            getLogChannel().error("Failed to load script asset {} for entity {}",
                    script.id().toString(), id.toString());
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
        const auto loaded =
                lua.safe_script(asset->source(), instance.environment, sol::script_pass_on_error);
        if (!loaded.valid()) {
            const sol::error error = loaded;
            getLogChannel().error("Failed to compile script {} for entity {}: {}",
                    script.id().toString(), id.toString(), error.what());
            instance.disabled = true;
            instance.created = true;
            auto [it, inserted] = instances.emplace(id, std::move(instance));
            (void)inserted;
            return &it->second;
        }

        auto [it, inserted] = instances.emplace(id, std::move(instance));
        (void)inserted;
        auto& live_instance = it->second;
        const sol::object entity = detail::makeEntityObject(lua, scene, id);
        if (!invoke(live_instance.environment, "on_create", entity, 0.0f, false, id)) {
            live_instance.disabled = true;
        }
        live_instance.created = true;
        return &live_instance;
    }

    // 给所有挂了脚本的实体派发一种回调。没有实例的就先建（并跑一次 on_create）。
    void dispatch(scene::Scene& scene, Callback which, float dt) {
        ensureBound(scene);

        // **先快照身份，再逐个重新验证。** 回调里可以 entity:destroy() 自己、也可以删别人，
        // 而那会动 EnTT 的存储 —— 边遍历 view 边执行 Lua 就是踩着自己的脚往前走。
        pending.clear();
        for (auto [handle, id, script]:
                scene.view<scene::IDComponent, ScriptComponent>().each()) {
            (void)handle;
            pending.emplace_back(id.id, script.script);
        }

        for (const auto& [id, script]: pending) {
            // 前一个回调可能把这个实体删了，或者把 ScriptComponent 摘了。
            if (!scene.findEntity(id).hasComponent<ScriptComponent>()) {
                continue;
            }
            auto found = instances.find(id);
            if (found == instances.end()) {
                createInstance(scene, id, script);
                found = instances.find(id);
                // on_create 自己就可能把这个实体删掉，那就别再往下派发了。
                if (!scene.findEntity(id).isValid()) {
                    continue;
                }
            }
            if (found == instances.end() || found->second.disabled) {
                continue;
            }
            const sol::object entity = detail::makeEntityObject(lua, scene, id);
            // 抛错就禁用这个实例，**两种回调一起禁**：一个手误不该只在一半的时钟上生效，
            // 不然「固定步不跑了但渲染帧还在跑」比彻底不跑更难查。
            if (!invoke(found->second.environment, callbackName(which), entity, dt, true, id)) {
                found->second.disabled = true;
            }
        }
    }

    // 实例还在、实体没了 → on_destroy，然后丢掉实例。
    //
    // 只在 Update 之后做一次，不在每个固定步都做：固定步里删掉的实体会在同一帧的 Update 之后
    // 收到 on_destroy，晚不过一帧。反过来（每个固定步收一次）要多两次全量遍历，换不来什么。
    void reap(scene::Scene& scene) {
        live.clear();
        for (auto [handle, id, script]:
                scene.view<scene::IDComponent, ScriptComponent>().each()) {
            (void)handle;
            (void)script;
            live.insert(id.id);
        }

        // on_destroy 里同样可以删别的实体，所以还是先收集再逐个跑。
        gone.clear();
        for (const auto& [id, instance]: instances) {
            (void)instance;
            if (!live.contains(id)) {
                gone.push_back(id);
            }
        }
        for (const core::UUID id: gone) {
            const auto found = instances.find(id);
            if (found == instances.end()) {
                continue;
            }
            if (!found->second.disabled && found->second.created) {
                const sol::object entity = detail::makeEntityObject(lua, scene, id);
                invoke(found->second.environment, "on_destroy", entity, 0.0f, false, id);
            }
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

void ScriptSystem::requestSessionReset() noexcept { m_impl->session_reset_requested = true; }

void ScriptSystem::onFixedUpdate(scene::Scene& scene, const scene::UpdateContext& context) {
    m_impl->ensureSession(context.frameIndex);
    m_impl->dispatch(scene, Callback::FixedUpdate, context.fixedDeltaTime.getSeconds());
}

void ScriptSystem::onUpdate(scene::Scene& scene, const scene::UpdateContext& context) {
    m_impl->ensureSession(context.frameIndex);
    m_impl->dispatch(scene, Callback::Update, context.deltaTime.getSeconds());
    m_impl->reap(scene);
}

} // namespace arti::engine
