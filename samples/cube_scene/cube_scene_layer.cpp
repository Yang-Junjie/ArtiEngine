#include "cube_scene_layer.h"

#include "artichoco/core/application.h"
#include "artichoco/platform/window/sdl_vulkan_surface_source.h"
#include "artichoco/renderer/render_device.h"
#include "artichoco/scene/entity.h"
#include "artichoco/scene/scene.h"
#include "imgui/imgui_host.h"

#include <array>
#include <cstddef>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <span>
#include <vector>

namespace arti::sample {
namespace {

// 立方体：每个面 4 个顶点，方便给独立的法线和 UV。
// 三角形按「从外面看逆时针」编写，与 ArtiRenderer 的 opaque pass 的正面约定一致。
rendering::Mesh makeCubeMesh() {
    struct FaceDesc {
        glm::vec3 normal;
        glm::vec3 origin;
        glm::vec3 right;
        glm::vec3 up;
    };

    constexpr std::array<FaceDesc, 6> faces{ {
        { { 1, 0, 0 }, { 1, -1, 1 }, { 0, 0, -2 }, { 0, 2, 0 } },
        { { -1, 0, 0 }, { -1, -1, -1 }, { 0, 0, 2 }, { 0, 2, 0 } },
        { { 0, 1, 0 }, { -1, 1, 1 }, { 2, 0, 0 }, { 0, 0, -2 } },
        { { 0, -1, 0 }, { -1, -1, -1 }, { 2, 0, 0 }, { 0, 0, 2 } },
        { { 0, 0, 1 }, { -1, -1, 1 }, { 2, 0, 0 }, { 0, 2, 0 } },
        { { 0, 0, -1 }, { 1, -1, -1 }, { -2, 0, 0 }, { 0, 2, 0 } },
    } };

    rendering::Mesh mesh;
    mesh.vertices.reserve(faces.size() * 4);
    mesh.indices.reserve(faces.size() * 6);

    for (const auto& face: faces) {
        const auto base = static_cast<uint32_t>(mesh.vertices.size());
        const std::array<glm::vec2, 4> uvs{ { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f },
            { 0.0f, 0.0f } } };
        const std::array<glm::vec3, 4> corners{ {
            face.origin,
            face.origin + face.right,
            face.origin + face.right + face.up,
            face.origin + face.up,
        } };

        for (size_t corner = 0; corner < corners.size(); ++corner) {
            rendering::MeshVertex vertex;
            vertex.position = corners[corner] * 0.5f;
            vertex.normal = face.normal;
            vertex.tangent = glm::normalize(face.right);
            vertex.bitangent = glm::normalize(face.up);
            vertex.uv = uvs[corner];
            mesh.vertices.push_back(vertex);
            mesh.bounds.expand(vertex.position);
        }

        for (const uint32_t offset: { 0U, 1U, 2U, 0U, 2U, 3U }) {
            mesh.indices.push_back(base + offset);
        }
    }

    return mesh;
}

std::vector<std::byte> makeCheckerTexels(uint32_t size) {
    std::vector<std::byte> texels(static_cast<size_t>(size) * size * 4);
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const bool light = ((x / 8) + (y / 8)) % 2 == 0;
            const auto value = static_cast<std::byte>(light ? 230 : 60);
            const size_t offset = (static_cast<size_t>(y) * size + x) * 4;
            texels[offset + 0] = value;
            texels[offset + 1] = value;
            texels[offset + 2] = value;
            texels[offset + 3] = static_cast<std::byte>(255);
        }
    }
    return texels;
}

} // namespace

CubeSceneLayer::CubeSceneLayer(bool enable_renderer, uint32_t frame_limit, bool editor_mode)
        : Layer("CubeSceneLayer"),
          m_enable_renderer(enable_renderer),
          m_frame_limit(frame_limit),
          m_editor_mode(editor_mode) {}

CubeSceneLayer::~CubeSceneLayer() = default;

void CubeSceneLayer::onAttach() {
    auto& app = core::Application::get();

    if (!m_enable_renderer) {
        app.getLogChannel().info("Renderer disabled, running window loop only");
        return;
    }

    auto surface_source = platform::createSDLVulkanSurfaceSource(app.getWindow());
    renderer::RenderDeviceCreateInfo device_info;
    device_info.application_name = "ArtiEngine";
    m_render_device = std::make_unique<renderer::RenderDevice>(app.getWindow(),
            std::move(surface_source), device_info);

    rendering::RendererCreateInfo renderer_info;
    renderer_info.present =
            m_editor_mode ? rendering::PresentMode::IntoUI : rendering::PresentMode::Direct;
    m_renderer = std::make_unique<rendering::Renderer>(*m_render_device, renderer_info);
    m_scene = std::make_unique<scene::Scene>();
    createSceneEntities();

    engine::ImGuiHostCreateInfo imgui_info;
    // 帧数受限说明是自动化跑的，布局要可复现，别继承也别写 imgui.ini。
    imgui_info.persist_layout = m_frame_limit == 0;
    m_imgui = std::make_unique<engine::ImGuiHost>(app.getWindow(), *m_renderer, imgui_info);

    const auto output = m_renderer->outputInfo();
    app.getLogChannel().info("Engine ready, output {}x{} (available: {})", output.width,
            output.height, output.available);
}

void CubeSceneLayer::createSceneEntities() {
    constexpr uint32_t checker_size = 64;
    const auto texels = makeCheckerTexels(checker_size);

    rendering::TextureDesc texture_desc;
    texture_desc.texels = std::span{ texels };
    texture_desc.width = checker_size;
    texture_desc.height = checker_size;
    texture_desc.format = rendering::TextureFormat::RGBA8Unorm;
    texture_desc.debug_name = "Sample checker";
    m_checker_texture = m_renderer->createTexture(texture_desc);

    rendering::Material material;
    material.type = rendering::MaterialType::BlinnPhong;
    material.base_color = glm::vec4{ 1.0f, 0.85f, 0.7f, 1.0f };
    material.base_color_texture = m_checker_texture;
    material.specular_color = glm::vec3{ 1.0f };
    material.specular_strength = 0.6f;
    material.shininess = 32.0f;
    m_cube_material = m_renderer->createMaterial(material);

    m_cube_mesh = m_renderer->createMesh(makeCubeMesh(), "Sample cube");

    // 相机。没有 aspect —— extract 会按渲染目标尺寸算。
    // view 矩阵是世界变换的逆，所以这里摆实体就等于摆相机。
    auto camera = m_scene->createEntity("Camera");
    auto& camera_transform = camera.getComponent<scene::TransformComponent>();
    camera_transform.translation = glm::vec3{ 0.0f, 1.5f, 4.0f };
    // 让相机看向原点：lookAt 给的是 view 矩阵，取逆才是相机的世界变换。
    const glm::mat4 world = glm::inverse(glm::lookAt(camera_transform.translation,
            glm::vec3{ 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f }));
    camera_transform.rotation = glm::quat_cast(world);
    camera.addComponent<engine::CameraComponent>();

    // 方向光。没有 direction 字段，方向是实体的 -Z 轴，所以用旋转来摆。
    auto sun = m_scene->createEntity("Sun");
    auto& sun_transform = sun.getComponent<scene::TransformComponent>();
    sun_transform.rotation =
            glm::quat{ glm::vec3{ glm::radians(-50.0f), glm::radians(-30.0f), 0.0f } };
    auto& sun_light = sun.addComponent<engine::DirectionalLightComponent>();
    sun_light.color = glm::vec3{ 1.0f, 0.96f, 0.9f };
    sun_light.intensity = 1.0f;

    // 三个立方体，横着排开。多于一个是为了确认 extract 真的在遍历，而不是碰巧画出一个。
    for (int index = -1; index <= 1; ++index) {
        auto cube = m_scene->createEntity("Cube");
        auto& transform = cube.getComponent<scene::TransformComponent>();
        transform.translation = glm::vec3{ static_cast<float>(index) * 1.6f, 0.0f, 0.0f };
        auto& mesh_renderer = cube.addComponent<engine::MeshRendererComponent>();
        mesh_renderer.mesh = m_cube_mesh;
        mesh_renderer.material = m_cube_material;
        m_spinning.push_back(cube.getComponent<scene::IDComponent>().id);
    }

    core::Application::get().getLogChannel().info("Scene built: {} spinning cubes",
            m_spinning.size());
}

void CubeSceneLayer::onDetach() {
    // waitIdle 要在销毁 ImGuiHost 之前：它的析构会销毁字体图集纹理，GPU 可能还在用。
    if (m_renderer) {
        m_renderer->waitIdle();
    }
    m_imgui.reset();
    m_scene.reset();
    m_renderer.reset();
    m_render_device.reset();
}

void CubeSceneLayer::onUpdate(core::Timestep delta_time) {
    m_elapsed_seconds += delta_time.getSeconds();
    ++m_frame_index;

    // 改的是组件，不是 RenderScene —— 世界变换的传播和抽取都交给下面那条链。
    if (m_scene && m_rotate) {
        float phase = 0.0f;
        for (const auto id: m_spinning) {
            auto entity = m_scene->findEntity(id);
            if (!entity.isValid()) {
                continue;
            }
            auto& transform = entity.getComponent<scene::TransformComponent>();
            transform.rotation =
                    glm::quat{ glm::vec3{ 0.0f, m_elapsed_seconds * 0.8f + phase, 0.0f } };
            phase += 0.6f;
        }
    }

    if (m_frame_limit != 0 && m_frame_index >= m_frame_limit) {
        core::Application::get().getLogChannel().info("Frame limit reached after {} frames",
                m_frame_index);
        core::Application::get().close();
    }
}

void CubeSceneLayer::onImGuiRender() {
    if (!m_imgui) {
        return;
    }

    m_imgui->beginFrame();
    // 停靠区要在其它窗口之前建。中央节点透传，所以 Direct 模式下场景直接从中间透上来。
    m_imgui->dockSpaceOverViewport();
    drawUI();
    if (m_editor_mode) {
        drawViewportPanel();
    }
    m_imgui->endFrame();
}

void CubeSceneLayer::drawUI() {
    ImGui::SetNextWindowSize(ImVec2{ 320.0f, 0.0f }, ImGuiCond_FirstUseEver);
    ImGui::Begin("ArtiEngine");

    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);

    ImGui::SeparatorText("Extract");
    ImGui::Text("entities:   %zu", m_spinning.size());
    ImGui::Text("draws:      %zu", m_extractor.renderScene().draws.size());
    ImGui::Text("lights:     %zu", m_extractor.renderScene().lights.size());
    ImGui::Text("draw calls: %u", m_last_statistics.draw_calls);
    ImGui::Text("has camera: %s", m_extractor.hasCamera() ? "yes" : "no");

    ImGui::SeparatorText("Present");
    if (ImGui::Checkbox("Editor mode (scene into panel)", &m_editor_mode)) {
        m_renderer->setPresentMode(
                m_editor_mode ? rendering::PresentMode::IntoUI : rendering::PresentMode::Direct);
        // 切回 Direct 时清掉请求尺寸，否则场景会一直按上次面板的大小渲染。
        if (!m_editor_mode) {
            m_scene_width = 0;
            m_scene_height = 0;
        }
    }
    ImGui::Checkbox("Rotate", &m_rotate);

    ImGui::End();
}

void CubeSceneLayer::drawViewportPanel() {
    // 必须给初始尺寸：Image 的大小取自 GetContentRegionAvail()，而窗口默认自适应内容 ——
    // 两者互相取值会塌缩到最小尺寸。停靠之后尺寸由 dock node 决定，这个循环依赖就消失了。
    ImGui::SetNextWindowSize(ImVec2{ 960.0f, 540.0f }, ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
    ImGui::Begin("Viewport");
    ImGui::PopStyleVar();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    m_scene_width = available.x > 0.0f ? static_cast<uint32_t>(available.x) : 0;
    m_scene_height = available.y > 0.0f ? static_cast<uint32_t>(available.y) : 0;

    if (m_scene_width != 0 && m_scene_height != 0) {
        // SceneColor 是线性数据，ImGuiPass 认出这个 id 后会跳过 sRGB 解码。
        ImGui::Image(m_renderer->sceneColorTextureId(),
                ImVec2{ static_cast<float>(m_scene_width), static_cast<float>(m_scene_height) });
    }

    ImGui::End();
}

void CubeSceneLayer::onRender() {
    if (!m_renderer || !m_scene) {
        return;
    }

    const auto output = m_renderer->outputInfo();
    if (!output.available) {
        return;
    }

    // 目标尺寸从这里进去：extract 靠它算 aspect。编辑器模式下是 Viewport 面板的尺寸，
    // 否则是窗口的 —— 这正是 aspect 不存在 CameraComponent 里的理由。
    m_renderer->setSceneTargetSize(m_scene_width, m_scene_height);

    engine::ExtractTarget target;
    const bool has_panel = m_scene_width != 0 && m_scene_height != 0;
    target.width = has_panel ? m_scene_width : output.width;
    target.height = has_panel ? m_scene_height : output.height;

    const auto& render_scene = m_extractor.extract(*m_scene, *m_renderer, target);
    if (!m_extractor.hasCamera()) {
        return;
    }

    const auto statistics = m_renderer->renderFrame(render_scene,
            m_imgui ? m_imgui->overlay() : rendering::FrameOverlay{});
    m_last_statistics = statistics;
    if (m_frame_index == 1 && statistics.rendered) {
        core::Application::get().getLogChannel().info(
                "First frame rendered ({} draw calls, {} lights)", statistics.draw_calls,
                render_scene.lights.size());
    }
}

} // namespace arti::sample
