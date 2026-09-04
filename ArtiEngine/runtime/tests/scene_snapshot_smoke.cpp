// 场景快照冒烟测试：World::captureScene() / restoreScene() 的幂等与往返。
//
// 为什么值得单独一个测试：编辑器的撤销栈拿「两段文本一样 ⇔ 场景一样」当变更检测
// （docs/tasks/2026-09-04-editor-undo-redo.md 的 D2），而那一条成立完全依赖 SceneSerializer
// 把实体按 UUID、组件按类型名都排过序（scene_serializer.cpp:36-48）。哪天有人为了省一次排序
// 把它去掉，症状不会是崩溃，而是编辑器里凭空冒出一堆「按了 Ctrl+Z 却什么都没变」的历史项 ——
// 从那个现象反推到「序列化的顺序在抖」非常远，所以在这里钉死。

#include "runtime/world.h"
#include "scene/components.h"

#include "artichoco/core/log.h"
#include "artichoco/core/uuid.h"
#include "artichoco/scene/components.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace arti;

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "scene_snapshot_smoke: " << message << '\n';
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

// 逐元素比较两个矩阵。**不用精确相等**：浮点要过一遍 YAML 的十进制文本，往返在原理上就允许
// 最后一两位有差别。1e-5 足够松，也足够严 —— 真丢了一个字段的话差的是量级，不是最后一位。
bool matricesClose(const glm::mat4& lhs, const glm::mat4& rhs) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::fabs(lhs[column][row] - rhs[column][row]) > 1e-5f) {
                return false;
            }
        }
    }
    return true;
}

struct Fixture {
    core::UUID root;
    core::UUID child;
    core::UUID light;
    core::UUID body;
};

// 同一批 UUID、同一批组件值，按给定顺序建出来。**唯一的变量是创建顺序** ——
// 用来验「dump 是规范形式」这条（见 canonicalFormHolds）。
void buildOrdered(scene::Scene& target, bool reverse) {
    constexpr core::UUID::Value kIds[] = { 0x1111ull, 0x2222ull, 0x3333ull, 0x4444ull };
    // 名字跟着 UUID 走，不跟着创建顺序走 —— 否则两个场景的内容本身就不一样了。
    const char* const kTags[] = { "A", "B", "C", "D" };

    for (int step = 0; step < 4; ++step) {
        const int index = reverse ? 3 - step : step;
        auto entity = target.createEntityWithUUID(core::UUID{ kIds[index] }, kTags[index]);
        entity.getComponent<scene::TransformComponent>().translation =
                glm::vec3{ static_cast<float>(index), 0.5f, -1.25f };
        entity.addComponent<engine::PointLightComponent>().range =
                2.0f + static_cast<float>(index);
    }
    // 层级要等实体都在了才能接，所以两个顺序都走同一条路。
    target.setParent(target.findEntity(core::UUID{ kIds[1] }), target.findEntity(core::UUID{ kIds[0] }));
}

// 两个内容相同、**创建顺序相反**的场景，dump 出来必须逐字节相同。
//
// 这是整个撤销栈的地基（D2）：变更检测就是比较两段文本，所以文本绝不能受「实体是按什么顺序
// 建出来的」影响 —— 而编辑器里那个顺序天天在变（建一个、删一个、EnTT 复用槽位）。
// 靠的是 scene_serializer.cpp:36-39 按 UUID 排序那一段。
//
// **上面那条「连续两次 capture 相同」抓不到这个** —— 一次进程里不动场景的话，
// 就算完全不排序，两次 dump 也一样。所以这一条是分开的。
bool canonicalFormHolds() {
    engine::World forward;
    buildOrdered(forward.scene(), /*reverse=*/false);

    engine::World backward;
    buildOrdered(backward.scene(), /*reverse=*/true);

    return forward.captureScene() == backward.captureScene();
}

// 一个刻意「每样来一点」的场景：两层父子、带旋转和非 1 缩放、四类组件、一个资产 handle。
//
// **字段值都刻意不取默认值。** 取默认值的话「序列化漏写了这个字段」会被反序列化时的默认值
// 悄悄补回来，往返测试就什么都抓不到。
Fixture populate(scene::Scene& target) {
    Fixture fixture;

    auto root = target.createEntity("Root");
    auto& root_transform = root.getComponent<scene::TransformComponent>();
    root_transform.translation = glm::vec3{ 1.25f, -2.5f, 3.75f };
    root_transform.rotation = glm::quat{ glm::vec3{ 0.3f, -0.7f, 1.1f } };
    root_transform.scale = glm::vec3{ 2.0f, 0.5f, 1.5f };
    auto& mesh_renderer = root.addComponent<engine::MeshRendererComponent>();
    mesh_renderer.mesh = arti::asset::AssetHandle<engine::asset::MeshAsset>{ core::UUID{ 0xabcd1234u } };
    mesh_renderer.materials.emplace_back(core::UUID{ 0x9876fedcu });
    mesh_renderer.visible = false;
    fixture.root = root.getUUID();

    auto child = target.createEntity("Child");
    auto& child_transform = child.getComponent<scene::TransformComponent>();
    child_transform.translation = glm::vec3{ 0.5f, 4.0f, -1.0f };
    child_transform.rotation = glm::quat{ glm::vec3{ -0.4f, 0.2f, 0.0f } };
    auto& camera = child.addComponent<engine::CameraComponent>();
    camera.fov_degrees = 42.5f;
    camera.near_plane = 0.05f;
    camera.far_plane = 512.0f;
    camera.primary = false;
    target.setParent(child, root);
    fixture.child = child.getUUID();

    auto light = target.createEntity("Sun");
    auto& directional = light.addComponent<engine::DirectionalLightComponent>();
    directional.color = glm::vec3{ 0.9f, 0.8f, 0.7f };
    directional.intensity = 3.25f;
    directional.casts_shadow = false;
    directional.shadow_distance = 37.5f;
    fixture.light = light.getUUID();

    auto body = target.createEntity("Crate");
    auto& rigid_body = body.addComponent<engine::RigidBodyComponent>();
    rigid_body.type = engine::RigidBodyComponent::Type::Kinematic;
    rigid_body.gravity_scale = 0.25f;
    rigid_body.enable_sleep = false;
    auto& collider = body.addComponent<engine::ColliderComponent>();
    collider.shape = engine::ColliderComponent::Shape::Capsule;
    collider.radius = 0.375f;
    collider.half_height = 1.125f;
    collider.friction = 0.8f;
    collider.restitution = 0.45f;
    fixture.body = body.getUUID();

    return fixture;
}

int run() {
    engine::World world;
    auto& target = world.scene();
    const Fixture fixture = populate(target);

    target.updateWorldTransforms();
    const glm::mat4 child_world_before = target.getWorldTransform(target.findEntity(fixture.child));
    const size_t count_before = entityCount(target);
    if (!require(count_before == 4, "夹具应该有 4 个实体")) {
        return 1;
    }

    // ---- 1. 幂等与规范形式 ----
    const std::string first = world.captureScene();
    if (!require(!first.empty(), "captureScene 返回了空串")) {
        return 1;
    }
    if (!require(first == world.captureScene(),
                "连续两次 captureScene 的文本不一样 —— 序列化不是规范形式，实体或组件的顺序在抖")) {
        return 1;
    }
    if (!require(canonicalFormHolds(),
                "同样的内容按不同顺序建出来，dump 不一样 —— 撤销栈的变更检测会被创建顺序骗到")) {
        return 1;
    }

    // ---- 2. 往返 ----
    // 刻意改三类东西：删一个实体、加一个实体、改一个字段。只改一类的话，恢复路径上某一类
    // 处理错了（比如「多出来的实体没被清掉」）就可能溜过去。
    target.destroyEntity(target.findEntity(fixture.light));
    auto added = target.createEntity("Added Later");
    added.addComponent<engine::PointLightComponent>().range = 12.5f;
    target.findEntity(fixture.child).getComponent<engine::CameraComponent>().fov_degrees = 90.0f;

    // 这一条是上面那条幂等的对照组：证明文本比较是**敏感**的，不只是稳定。少了它，
    // 一个恒返回同一段文本的 captureScene 也能让整个测试全绿。
    if (!require(world.captureScene() != first,
                "改了场景之后文本没变 —— 变更检测是瞎的，撤销栈会永远记不下任何东西")) {
        return 1;
    }

    if (!require(world.restoreScene(first), "restoreScene 失败")) {
        return 1;
    }
    if (!require(world.captureScene() == first, "往返之后的文本和原来不一致")) {
        return 1;
    }

    // ---- 3. UUID 和层级都活着 ----
    // 编辑器的选中、拾取 id 表、面板的每帧查找全是按 UUID 走的，这一条塌了它们就都成了悬空引用。
    if (!require(entityCount(target) == count_before, "往返之后实体数不对")) {
        return 1;
    }
    auto root = target.findEntity(fixture.root);
    auto child = target.findEntity(fixture.child);
    auto light = target.findEntity(fixture.light);
    if (!require(root.isValid() && child.isValid() && light.isValid(),
                "往返之后原来的 UUID 找不到了")) {
        return 1;
    }
    if (!require(target.getParent(child) == root, "往返之后父子关系断了")) {
        return 1;
    }
    if (!require(target.findEntity(fixture.child)
                            .getComponent<engine::CameraComponent>()
                            .fov_degrees < 50.0f,
                "往返之后组件字段没有回到快照里的值")) {
        return 1;
    }

    // 世界变换是派生数据。这一条实际上在验 deserialize 结尾那句 updateWorldTransforms()
    // （scene_serializer.cpp:201）—— 少了它，恢复出来的实体位置要等到下一次有人标脏才对。
    if (!require(matricesClose(target.getWorldTransform(child), child_world_before),
                "往返之后子实体的世界变换不对（updateWorldTransforms 没跑？）")) {
        return 1;
    }

    // ---- 4. 恢复失败时场景一点不变 ----
    // 这是 restoreScene 和 loadScene 的关键区别（见 world.h 的注释）：历史项坏了不能连坐，
    // 把用户正在编辑的场景清空是纯粹的雪上加霜。
    const std::string good = world.captureScene();

    if (!require(!world.restoreScene("这不是 YAML: ["), "畸形 YAML 居然恢复成功了")) {
        return 1;
    }
    if (!require(!world.restoreScene("Entities: 3"), "Entities 不是序列时居然恢复成功了")) {
        return 1;
    }
    // 结构合法、YAML 也解析得动，但内容过不了校验 —— 这条走的是「解析到一半才失败」那条路，
    // 和上面两条（连 YAML 都不成立）不是同一段代码。
    std::string unknown_component = good;
    if (const auto at = unknown_component.find("arti.tag"); at != std::string::npos) {
        unknown_component.replace(at, std::string_view{ "arti.tag" }.size(), "arti.nope");
    }
    if (!require(unknown_component != good, "测试自身有问题：夹具文本里没有 arti.tag")) {
        return 1;
    }
    if (!require(!world.restoreScene(unknown_component), "缺必需组件的文本居然恢复成功了")) {
        return 1;
    }

    if (!require(world.captureScene() == good, "三次失败的恢复动了场景")) {
        return 1;
    }

    // ---- 5. 空场景 ----
    // 新建项目、读场景失败之后都是这个状态，撤销栈的基线可能正好是它。
    world.clear();
    const std::string empty = world.captureScene();
    if (!require(!empty.empty(), "空场景 capture 出来是空串")) {
        return 1;
    }
    if (!require(world.restoreScene(empty) && entityCount(target) == 0,
                "空场景往返之后不是空的")) {
        return 1;
    }
    // 空 → 有内容 → 空，两个方向都要能走。
    if (!require(world.restoreScene(good) && entityCount(target) == count_before,
                "从空场景恢复回有内容的快照失败")) {
        return 1;
    }
    if (!require(world.restoreScene(empty) && entityCount(target) == 0,
                "恢复回空快照之后场景没被清空")) {
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
