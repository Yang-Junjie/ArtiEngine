#include "runtime/asset_runtime.h"

#include "asset/builtin_assets.h"
#include "asset/loaders/material_loader.h"
#include "asset/loaders/mesh_loader.h"
#include "asset/loaders/prefab_loader.h"
#include "asset/loaders/script_loader.h"
#include "asset/loaders/texture_loader.h"
#include "engine_log.h"

#include "artichoco/asset/asset_manager.h"

#include <stdexcept>
#include <utility>

namespace arti::engine {

AssetRuntime::AssetRuntime() = default;

AssetRuntime::~AssetRuntime() { close(); }

bool AssetRuntime::open(const std::filesystem::path& assets_root,
        const std::filesystem::path& artifacts_root) {
    close();

    auto manager = std::make_unique<arti::asset::AssetManager>();
    if (!manager->open(assets_root, artifacts_root)) {
        getLogChannel().error("Failed to open the asset workspace at '{}'", assets_root.string());
        return false;
    }
    return finishOpen(std::move(manager));
}

bool AssetRuntime::openPackaged(const std::filesystem::path& artifacts_root,
        const std::filesystem::path& manifest_file) {
    close();

    auto manager = std::make_unique<arti::asset::AssetManager>();
    if (!manager->openPackaged(artifacts_root, manifest_file)) {
        // openPackaged 已经记了具体原因（manifest 读不到 / 解析失败 / Library 不存在）。
        return false;
    }
    return finishOpen(std::move(manager));
}

bool AssetRuntime::finishOpen(std::unique_ptr<arti::asset::AssetManager> manager) {
    const bool loaders_registered =
            manager->registerLoader(std::make_unique<asset::MeshLoader>()) &&
            manager->registerLoader(std::make_unique<asset::MaterialLoader>()) &&
            manager->registerLoader(std::make_unique<asset::TextureLoader>()) &&
            manager->registerLoader(std::make_unique<asset::PrefabLoader>()) &&
            manager->registerLoader(std::make_unique<asset::ScriptLoader>());
    if (!loaders_registered) {
        getLogChannel().error("Failed to register the engine asset loaders");
        return false;
    }

    // builtin 的 artifact 补齐必须在这里做，不能只靠 reconcile —— 运行时不 reconcile，
    // 而默认材质和立方体网格是场景里随时可能引用的东西。
    //
    // 打包模式下这一步通常什么都不写：打包会把 Builtin/ 下的 artifact 一起带上，
    // hasArtifact() 命中就跳过写盘 —— 装在只读目录里的游戏也能起来。
    if (!asset::ensureBuiltinAssets(*manager)) {
        getLogChannel().error("Failed to restore the builtin assets");
        return false;
    }

    m_manager = std::move(manager);
    return true;
}

void AssetRuntime::close() noexcept {
    if (m_manager) {
        m_manager->close();
    }
    m_manager.reset();
}

bool AssetRuntime::isOpen() const noexcept {
    return m_manager != nullptr && m_manager->storage().isOpen();
}

bool AssetRuntime::isPackaged() const noexcept {
    return m_manager != nullptr && m_manager->isPackaged();
}

arti::asset::AssetManager& AssetRuntime::manager() {
    if (!m_manager) {
        throw std::logic_error("AssetRuntime is not open");
    }
    return *m_manager;
}

const arti::asset::AssetManager& AssetRuntime::manager() const {
    if (!m_manager) {
        throw std::logic_error("AssetRuntime is not open");
    }
    return *m_manager;
}

} // namespace arti::engine
