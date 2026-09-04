#include "scene/prefab_instantiation.h"

#include "asset/builtin_assets.h"
#include "asset/material_asset.h"
#include "asset/mesh_asset.h"
#include "asset/prefab_asset.h"
#include "engine_log.h"
#include "scene/components.h"

#include "artichoco/scene/components.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <cstddef>
#include <vector>

namespace arti::engine {

scene::Entity instantiatePrefab(scene::Scene& scene, const asset::PrefabAsset& prefab) {
    const auto& nodes = prefab.nodes();
    if (nodes.empty()) {
        getLogChannel().warn("instantiatePrefab: prefab {} has no nodes",
                prefab.getHandle().toString());
        return {};
    }

    std::vector<scene::Entity> created;
    created.reserve(nodes.size());
    for (const auto& node: nodes) {
        auto entity = scene.createEntity(node.name.empty() ? "Prefab Node" : node.name);
        auto& transform = entity.getComponent<scene::TransformComponent>();
        glm::vec3 translation{ 0.0f };
        glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 scale{ 1.0f };
        glm::vec3 skew{ 0.0f };
        glm::vec4 perspective{ 1.0f };
        if (glm::decompose(node.local_transform, scale, rotation, translation, skew,
                    perspective)) {
            transform.translation = translation;
            transform.rotation = rotation;
            transform.scale = scale;
        } else {
            getLogChannel().warn(
                    "instantiatePrefab: could not decompose the local transform of node '{}'",
                    node.name);
        }
        if (node.mesh.isValid()) {
            auto& mesh_renderer = entity.addComponent<MeshRendererComponent>();
            mesh_renderer.mesh = arti::asset::AssetHandle<asset::MeshAsset>{ node.mesh };
            for (const auto material: node.materials) {
                mesh_renderer.materials.push_back(
                        arti::asset::AssetHandle<asset::MaterialAsset>{ material });
            }
            if (mesh_renderer.materials.empty()) {
                mesh_renderer.materials.push_back(
                        arti::asset::AssetHandle<asset::MaterialAsset>{
                                asset::kBuiltinDefaultMaterial });
            }
        }
        created.push_back(entity);
    }

    for (size_t index = 0; index < created.size(); ++index) {
        const uint32_t parent = nodes[index].parent;
        if (parent != asset::kNoParentNode && parent < created.size()) {
            scene.setParent(created[index], created[parent]);
        }
    }

    return created.front();
}

} // namespace arti::engine
