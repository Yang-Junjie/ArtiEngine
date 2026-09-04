// Prefab 实例化冒烟：不碰磁盘、不碰渲染，只确认节点树变成了正确的实体层级。
//
// 这段逻辑以前只活在编辑器的 spawnAssetEntity 里。脚本 spawn_prefab 和播放器都要走同一份，
// 所以下沉到 Engine 之后必须有一个不依赖窗口的测试钉住它 —— 否则编辑器里「看起来能拖」
// 和「脚本生成出来缺父子 / 缺材质」会各自漂移。

#include "asset/builtin_assets.h"
#include "asset/prefab_asset.h"
#include "scene/components.h"
#include "scene/prefab_instantiation.h"

#include "artichoco/core/log.h"
#include "artichoco/core/uuid.h"
#include "artichoco/scene/components.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace arti;

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "prefab_instantiate_smoke: " << message << '\n';
    }
    return condition;
}

size_t entityCount(scene::Scene& target) {
    size_t count = 0;
    for (auto [handle, id]: target.view<scene::IDComponent>().each()) {
        (void)handle;
        (void)id;
        ++count;
    }
    return count;
}

int run() {
    // ---- 空 prefab 不往场景里塞东西 ----
    {
        scene::Scene scene;
        const engine::asset::PrefabAsset empty{ core::UUID{ 0xaaaull }, {} };
        const auto root = engine::instantiatePrefab(scene, empty);
        if (!require(!root.isValid(), "空 prefab 居然返回了有效实体")) {
            return 1;
        }
        if (!require(entityCount(scene) == 0, "空 prefab 往场景里塞了实体")) {
            return 1;
        }
    }

    // ---- 两节点：根带 mesh、子带偏移，父子接上 ----
    const core::UUID mesh_id{ engine::asset::kBuiltinCubeMesh };
    const core::UUID material_id{ 0x1234ull };

    engine::asset::PrefabNode root_node;
    root_node.name = "Root";
    root_node.local_transform = glm::translate(glm::mat4{ 1.0f }, glm::vec3{ 1.0f, 2.0f, 3.0f });
    root_node.mesh = mesh_id;
    root_node.materials = { material_id };

    engine::asset::PrefabNode child_node;
    child_node.name = "Child";
    child_node.parent = 0;
    child_node.local_transform = glm::translate(glm::mat4{ 1.0f }, glm::vec3{ 0.5f, 0.0f, 0.0f });

    const engine::asset::PrefabAsset prefab{ core::UUID{ 0xbbbull },
        { std::move(root_node), std::move(child_node) } };

    scene::Scene scene;
    const auto root = engine::instantiatePrefab(scene, prefab);
    if (!require(root.isValid(), "两节点 prefab 返回了无效实体")) {
        return 1;
    }
    if (!require(entityCount(scene) == 2, "实例化后实体数不是 2")) {
        return 1;
    }
    if (!require(root.getComponent<scene::TagComponent>().tag == "Root",
                "根节点的名字不对")) {
        return 1;
    }

    const auto& transform = root.getComponent<scene::TransformComponent>();
    if (!require(std::fabs(transform.translation.x - 1.0f) < 1e-5f &&
                        std::fabs(transform.translation.y - 2.0f) < 1e-5f &&
                        std::fabs(transform.translation.z - 3.0f) < 1e-5f,
                "根节点的 translation 没从矩阵里分解出来")) {
        return 1;
    }

    if (!require(root.hasComponent<engine::MeshRendererComponent>(), "根节点没挂 MeshRenderer")) {
        return 1;
    }
    const auto& mesh_renderer = root.getComponent<engine::MeshRendererComponent>();
    if (!require(mesh_renderer.mesh.id() == mesh_id, "根节点的 mesh handle 不对")) {
        return 1;
    }
    if (!require(mesh_renderer.materials.size() == 1 &&
                        mesh_renderer.materials.front().id() == material_id,
                "根节点的材质槽不对")) {
        return 1;
    }

    const auto children = scene.getChildren(root);
    if (!require(children.size() == 1, "根节点的子实体数不是 1")) {
        return 1;
    }
    if (!require(children.front().getComponent<scene::TagComponent>().tag == "Child",
                "子节点的名字不对")) {
        return 1;
    }
    if (!require(scene.getParent(children.front()) == root, "父子关系没接上")) {
        return 1;
    }

    const auto& child_transform = children.front().getComponent<scene::TransformComponent>();
    if (!require(std::fabs(child_transform.translation.x - 0.5f) < 1e-5f,
                "子节点的 translation 没从矩阵里分解出来")) {
        return 1;
    }

    // 子节点没有 mesh，就不该挂 MeshRenderer —— 空节点只贡献层级。
    if (!require(!children.front().hasComponent<engine::MeshRendererComponent>(),
                "没有 mesh 的节点居然挂了 MeshRenderer")) {
        return 1;
    }

    // ---- 没有材质的 mesh 节点回落到 builtin default ----
    {
        engine::asset::PrefabNode node;
        node.name = "BareMesh";
        node.mesh = mesh_id;
        const engine::asset::PrefabAsset bare{ core::UUID{ 0xccull }, { std::move(node) } };
        scene::Scene other;
        const auto entity = engine::instantiatePrefab(other, bare);
        const auto& renderer = entity.getComponent<engine::MeshRendererComponent>();
        if (!require(renderer.materials.size() == 1 &&
                            renderer.materials.front().id() == engine::asset::kBuiltinDefaultMaterial,
                    "没有材质的 mesh 节点没有回落到 builtin default")) {
            return 1;
        }
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
