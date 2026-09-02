// box3d 冒烟测试：不碰引擎，只让一个盒子在纯 C 的世界里掉下来。
//
// 为什么值得单独一个测试：box3d 的 submodule 跟 main（见 docs/tasks/2026-09-02-physics-box3d.md
// 的 D1），所以 `git submodule update --remote` 之后第一个该报警的是这里，而不是引擎里某处莫名
// 的行为。因此它刻意把后面几个阶段要用的 API 都碰一遍 —— 三种形状的创建函数、
// b3World_GetBodyEvents 的字段、userData 塞 64 位 id、四元数的分量顺序 —— 而不只是让盒子掉。

#include <box3d/box3d.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "physics_smoke: " << message << '\n';
    }
    return condition;
}

// 引擎那边要往 userData 里塞实体的 UUID（64 位），而 userData 是 void*。阶段 3.2 照这个做。
static_assert(sizeof(void*) >= sizeof(uint64_t), "userData 塞不下 64 位实体 id");

void* toUserData(uint64_t value) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(value));
}

uint64_t fromUserData(void* pointer) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
}

// 占满 64 位，这样「只有低 32 位活下来」这类错也会被抓到。
constexpr uint64_t kBoxUserData = 0xfedcba9876543210ull;

int run() {
    const int bytesBefore = b3GetByteCount();

    b3WorldDef worldDef = b3DefaultWorldDef();
    if (!require(worldDef.gravity.y < 0.0f, "默认重力不是朝下的")) {
        return 1;
    }
    const b3WorldId world = b3CreateWorld(&worldDef);

    // 地面：b3DefaultBodyDef 默认就是静态。半高 10、中心在 y = -10，上表面正好在 y = 0。
    b3BodyDef groundDef = b3DefaultBodyDef();
    groundDef.position = b3Vec3{ 0.0f, -10.0f, 0.0f };
    const b3BodyId ground = b3CreateBody(world, &groundDef);
    const b3BoxHull groundHull = b3MakeBoxHull(50.0f, 10.0f, 50.0f);
    b3ShapeDef groundShapeDef = b3DefaultShapeDef();
    b3CreateHullShape(ground, &groundShapeDef, &groundHull.base);

    // 会掉的那个：半长 1 的立方体，所以停下来时中心应该在 y = 1。
    b3BodyDef boxDef = b3DefaultBodyDef();
    boxDef.type = b3_dynamicBody;  // 不设就不会动
    boxDef.position = b3Vec3{ 0.0f, 4.0f, 0.0f };
    boxDef.userData = toUserData(kBoxUserData);
    const b3BodyId box = b3CreateBody(world, &boxDef);
    const b3BoxHull boxHull = b3MakeCubeHull(1.0f);
    b3ShapeDef boxShapeDef = b3DefaultShapeDef();
    boxShapeDef.density = 1.0f;  // dynamic body 至少要一个非零密度的 shape
    boxShapeDef.baseMaterial.friction = 0.3f;
    b3CreateHullShape(box, &boxShapeDef, &boxHull.base);

    // 球和胶囊在这里的作用是钉住「这两个创建函数叫什么、吃什么结构」—— 阶段 2 的
    // ColliderComponent 要用。放得离盒子远一点，免得撞在一起影响上面那条断言。
    b3BodyDef sphereDef = b3DefaultBodyDef();
    sphereDef.type = b3_dynamicBody;
    sphereDef.position = b3Vec3{ 4.0f, 4.0f, 0.0f };
    const b3BodyId sphereBody = b3CreateBody(world, &sphereDef);
    const b3Sphere sphere{ b3Vec3{ 0.0f, 0.0f, 0.0f }, 0.5f };  // 局部中心 + 半径
    b3ShapeDef sphereShapeDef = b3DefaultShapeDef();
    sphereShapeDef.density = 1.0f;
    b3CreateSphereShape(sphereBody, &sphereShapeDef, &sphere);

    b3BodyDef capsuleDef = b3DefaultBodyDef();
    capsuleDef.type = b3_dynamicBody;
    capsuleDef.position = b3Vec3{ -4.0f, 4.0f, 0.0f };
    const b3BodyId capsuleBody = b3CreateBody(world, &capsuleDef);
    // 胶囊是两个半球心 + 一个半径，不是「半高 + 半径」：立起来、总高 2 * (0.5 + 0.5) = 2。
    const b3Capsule capsule{ b3Vec3{ 0.0f, -0.5f, 0.0f }, b3Vec3{ 0.0f, 0.5f, 0.0f }, 0.5f };
    b3ShapeDef capsuleShapeDef = b3DefaultShapeDef();
    capsuleShapeDef.density = 1.0f;
    b3CreateCapsuleShape(capsuleBody, &capsuleShapeDef, &capsule);

    // 90 步 @ 1/60 = 1.5 秒。从 y=4 掉到 y=1 是 3 米、约 0.78 秒，剩下的时间够它停稳。
    bool sawBoxMoveEvent = false;
    for (int step = 0; step < 90; ++step) {
        b3World_Step(world, 1.0f / 60.0f, 4);

        // 写回路径的形状（阶段 3.3 照这个做）：这一步动过的 body 连续排在 moveEvents 里，
        // 带着 userData 和世界变换。数据只在下一次 step 之前有效，所以当场消费完、别存指针。
        const b3BodyEvents events = b3World_GetBodyEvents(world);
        for (int index = 0; index < events.moveCount; ++index) {
            const b3BodyMoveEvent& move = events.moveEvents[index];
            if (fromUserData(move.userData) != kBoxUserData) {
                continue;
            }
            sawBoxMoveEvent = true;
            // 直着掉，朝向应该一直是单位四元数。b3Quat 是 .v（xyz）+ .s（标量）两段 ——
            // 这里钉的就是这个分量顺序，引擎那边 glm::quat{ q.s, q.v.x, q.v.y, q.v.z } 靠它。
            if (!require(move.transform.q.s > 0.99f,
                        "直着掉的盒子朝向不是单位四元数（s=" +
                                std::to_string(move.transform.q.s) + "）")) {
                return 1;
            }
        }
    }

    if (!require(sawBoxMoveEvent, "90 步里没有一条属于盒子的移动事件")) {
        return 1;
    }

    const b3Vec3 boxPosition = b3Body_GetPosition(box);
    if (!require(boxPosition.y > 0.9f && boxPosition.y < 1.1f,
                "盒子没停在 y≈1（实际 " + std::to_string(boxPosition.y) + "）")) {
        return 1;
    }
    if (!require(std::abs(boxPosition.x) < 0.1f && std::abs(boxPosition.z) < 0.1f,
                "盒子在水平方向漂了")) {
        return 1;
    }

    const b3Vec3 spherePosition = b3Body_GetPosition(sphereBody);
    if (!require(spherePosition.y > 0.4f && spherePosition.y < 0.6f,
                "球没停在 y≈0.5（实际 " + std::to_string(spherePosition.y) + "）")) {
        return 1;
    }

    // 胶囊躺下还是立着都算过：范围只排除「穿地」和「停在半空」。它立着停在 y=1、
    // 倒了停在 y=0.5，两种都是物理上对的，钉死一个值只会让测试变脆。
    const b3Vec3 capsulePosition = b3Body_GetPosition(capsuleBody);
    if (!require(capsulePosition.y > 0.4f && capsulePosition.y < 1.1f,
                "胶囊穿地或停在半空（实际 y=" + std::to_string(capsulePosition.y) + "）")) {
        return 1;
    }

    b3DestroyWorld(world);

    // 拆世界要真的拆干净：D5 里每次进 Simulate / Play 都会重建世界，这里漏了就是每按一次
    // 播放涨一截。
    if (!require(!b3World_IsValid(world), "世界拆掉之后 world id 还是有效的")) {
        return 1;
    }
    const int bytesAfter = b3GetByteCount();
    if (!require(bytesAfter == bytesBefore,
                "拆世界之后还剩 " + std::to_string(bytesAfter - bytesBefore) + " 字节没还")) {
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    return run();
}
