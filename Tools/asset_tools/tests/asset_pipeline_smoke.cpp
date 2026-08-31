#include "asset_tools/asset_pipeline.h"

#include "artichoco/core/log.h"
#include "artichoco/core/uuid.h"
#include "asset/builtin_assets.h"
#include "asset/material_asset.h"
#include "asset/prefab_asset.h"
#include "asset/texture_asset.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <ranges>
#include <vector>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "asset_pipeline_smoke: " << message << '\n';
    }
    return condition;
}

bool writeText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output << text;
    return output.good();
}

size_t countMetadataFiles(const std::filesystem::path& root) {
    size_t count = 0;
    std::error_code error;
    for (const auto& entry: std::filesystem::recursive_directory_iterator{ root, error }) {
        if (error) {
            break;
        }
        if (entry.is_regular_file(error) &&
                entry.path().extension() == arti::asset::kAssetMetadataExtension) {
            ++count;
        }
    }
    return count;
}

size_t countFiles(const std::filesystem::path& root) {
    size_t count = 0;
    std::error_code error;
    for (const auto& entry: std::filesystem::recursive_directory_iterator{ root, error }) {
        if (error) {
            break;
        }
        if (entry.is_regular_file(error)) {
            ++count;
        }
    }
    return count;
}

std::string base64(const std::vector<unsigned char>& bytes) {
    constexpr std::string_view alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string text;
    for (size_t index = 0; index < bytes.size(); index += 3) {
        const uint32_t a = bytes[index];
        const uint32_t b = index + 1 < bytes.size() ? bytes[index + 1] : 0;
        const uint32_t c = index + 2 < bytes.size() ? bytes[index + 2] : 0;
        const uint32_t triple = (a << 16) | (b << 8) | c;
        text.push_back(alphabet[(triple >> 18) & 0x3F]);
        text.push_back(alphabet[(triple >> 12) & 0x3F]);
        text.push_back(index + 1 < bytes.size() ? alphabet[(triple >> 6) & 0x3F] : '=');
        text.push_back(index + 2 < bytes.size() ? alphabet[triple & 0x3F] : '=');
    }
    return text;
}

// 一个三角形的 buffer：3 个 vec3 position（36 字节）+ 3 个 uint16 index（6 字节）。
std::vector<unsigned char> triangleBuffer() {
    const std::array<float, 9> positions{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
    const std::array<uint16_t, 3> indices{ 0, 1, 2 };

    std::vector<unsigned char> bytes(sizeof(positions) + sizeof(indices));
    std::memcpy(bytes.data(), positions.data(), sizeof(positions));
    std::memcpy(bytes.data() + sizeof(positions), indices.data(), sizeof(indices));
    return bytes;
}

// 最小但可导入的 glTF：一个三角形 + 一个材质，材质把外部图片绑在指定槽位上。
// prescan 只读 JSON，但 import 需要真实几何，所以 buffer 走 data URI。
std::string minimalGltf(std::string_view image_uri, std::string_view slot) {
    std::string text = R"({
  "asset": { "version": "2.0" },
  "images": [ { "uri": "__URI__" } ],
  "samplers": [ {} ],
  "textures": [ { "source": 0, "sampler": 0 } ],
  "materials": [ { __SLOT__ } ],
  "buffers": [ { "byteLength": 42, "uri": "data:application/octet-stream;base64,__DATA__" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 6 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ],
  "meshes": [ { "name": "tri", "primitives": [
    { "attributes": { "POSITION": 0 }, "indices": 1, "material": 0 } ] } ],
  "nodes": [ { "mesh": 0 } ],
  "scenes": [ { "nodes": [ 0 ] } ],
  "scene": 0
})";
    const std::string slot_json = slot == "normal"
            ? std::string{ "\"normalTexture\": { \"index\": 0 }" }
            : std::string{ "\"pbrMetallicRoughness\": { \"baseColorTexture\": "
                           "{ \"index\": 0 } }" };
    text.replace(text.find("__URI__"), 7, std::string{ image_uri });
    text.replace(text.find("__SLOT__"), 8, slot_json);
    text.replace(text.find("__DATA__"), 8, base64(triangleBuffer()));
    return text;
}

// 1x1 未压缩 TGA。选 TGA 而不是 PNG：18 字节头 + 裸像素，没有 CRC 和
// zlib 流可写错。
const std::vector<unsigned char>& onePixelImage() {
    static const std::vector<unsigned char> bytes{
        0x00,        // id length
        0x00,        // no colour map
        0x02,        // uncompressed true-colour
        0x00, 0x00, 0x00, 0x00, 0x00,  // colour map spec
        0x00, 0x00,  // x origin
        0x00, 0x00,  // y origin
        0x01, 0x00,  // width  = 1
        0x01, 0x00,  // height = 1
        0x20,        // 32 bits per pixel
        0x00,        // descriptor
        0x40, 0x80, 0xC0, 0xFF,  // one BGRA pixel
    };
    return bytes;
}

bool writeBinary(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

constexpr std::string_view kTriangle = "o Triangle\n"
                                       "v 0 0 0\n"
                                       "v 1 0 0\n"
                                       "v 0 1 0\n"
                                       "vt 0 0\n"
                                       "vt 1 0\n"
                                       "vt 0 1\n"
                                       "vn 0 0 1\n"
                                       "f 1/1/1 2/2/1 3/3/1\n";

int run() {
    TemporaryDirectory temporary{ std::filesystem::temp_directory_path() /
                                  ("ArtiAssetPipeline-" +
                                          arti::core::UUID::generate().toString()) };
    const auto assets = temporary.path / "Assets";
    const auto artifacts = temporary.path / "Artifacts";
    std::error_code error;
    std::filesystem::create_directories(assets, error);
    if (!require(!error, "failed to create the temporary Assets directory")) {
        return 1;
    }

    arti::tools::asset::AssetPipeline pipeline;
    if (!require(pipeline.open(assets, artifacts), "failed to open the asset pipeline")) {
        return 1;
    }

    // Step 4: builtin 资产在 catalog 里，但不往 Assets/ 写任何 .meta。
    if (!require(pipeline.engineAssets().size() == 2, "builtin assets were not registered") ||
            !require(countMetadataFiles(assets) == 0,
                    "builtin assets wrote .meta sidecars into Assets/")) {
        return 1;
    }
    if (!require(pipeline.canImport("triangle.OBJ"),
                "OBJ extension matching is not case-insensitive") ||
            !require(!pipeline.canImport("notes.txt"), "unsupported extension was accepted")) {
        return 1;
    }

    if (!require(writeText(assets / "triangle.obj", kTriangle), "failed to write OBJ fixture") ||
            !require(writeText(assets / "notes.txt", "unsupported\n"),
                    "failed to write unsupported fixture")) {
        return 1;
    }

    // Step 2: 首轮 reconcile 应该导入 OBJ、把 txt 记成 unsupported。
    {
        const auto plan = pipeline.planReconcile();
        if (!require(plan.complete(), "the first plan did not complete") ||
                !require(plan.countWithAction(arti::asset::ReconcileAction::Import) == 1,
                        "the plan did not schedule the OBJ for import") ||
                !require(plan.countWithAction(arti::asset::ReconcileAction::Unsupported) == 1,
                        "the plan did not mark the txt as unsupported") ||
                !require(plan.orphans.empty() && plan.conflicts.empty() &&
                                plan.metadata_issues.empty(),
                        "a clean workspace produced orphans, conflicts or bad metadata")) {
            return 1;
        }
    }

    const auto first = pipeline.reconcile();
    if (!require(first.succeeded(), "the first reconcile failed") ||
            !require(first.imported == 1, "the first reconcile did not import the OBJ") ||
            !require(first.unsupported == 1, "the first reconcile miscounted unsupported files")) {
        return 1;
    }

    const auto triangle = pipeline.sourceAssets("triangle.obj");
    if (!require(triangle.state == arti::tools::asset::SourceState::Imported,
                "the imported OBJ is not in the Imported state") ||
            !require(triangle.assets.size() > 1,
                    "the OBJ did not produce sub-assets (mesh + prefab)") ||
            !require(pipeline.isImported("triangle.obj"), "imported source was not indexed")) {
        return 1;
    }

    // 一源一 sidecar：N 个子资产只产生一份 .meta。
    if (!require(countMetadataFiles(assets) == 1,
                "one source file must produce exactly one .meta sidecar")) {
        return 1;
    }
    // local_id 用源文件里的名字（OBJ 的 "o Triangle"），不是下标。
    bool named_mesh = false;
    for (const auto& asset: triangle.assets) {
        if (asset.local_id == "mesh.Triangle") {
            named_mesh = true;
        }
        if (!require(!asset.local_id.empty(),
                    "sub-assets must carry a non-empty local_id")) {
            return 1;
        }
    }
    if (!require(named_mesh, "the mesh local_id did not use the OBJ object name")) {
        return 1;
    }
    if (!require(pipeline.sourceAssets("notes.txt").state ==
                        arti::tools::asset::SourceState::Unsupported,
                "the txt file is not in the Unsupported state")) {
        return 1;
    }
    if (!require(pipeline.checkIntegrity().succeeded(),
                "freshly imported assets failed the integrity check")) {
        return 1;
    }

    // 第二轮应该完全无操作。
    {
        const auto plan = pipeline.planReconcile();
        if (!require(!plan.hasWork(), "a second plan still reported work to do") ||
                !require(plan.countWithAction(arti::asset::ReconcileAction::Current) == 1,
                        "the second plan did not report the OBJ as current")) {
            return 1;
        }
        const auto report = pipeline.reconcile();
        if (!require(report.imported == 0 && report.reimported == 0,
                    "the second reconcile reimported a current source")) {
            return 1;
        }
    }

    // 核心不变式：Library/ 可以整个删掉并重建。
    std::filesystem::remove_all(artifacts, error);
    if (!require(!error, "failed to delete the artifacts root")) {
        return 1;
    }
    {
        const auto plan = pipeline.planReconcile();
        if (!require(plan.countWithAction(arti::asset::ReconcileAction::Reimport) == 1,
                    "deleting the artifacts root did not schedule a reimport")) {
            return 1;
        }
        const auto report = pipeline.reconcile();
        if (!require(report.succeeded() && report.reimported == 1,
                    "the reimport after deleting artifacts failed") ||
                !require(pipeline.checkIntegrity().succeeded(),
                        "artifacts were not restored after the reimport")) {
            return 1;
        }
    }

    // Step 4 的另一半：builtin artifact 被删掉后重开项目应该重新生成。
    pipeline.close();
    std::filesystem::remove_all(artifacts / "Builtin", error);
    if (!require(!error, "failed to delete the builtin artifacts")) {
        return 1;
    }
    if (!require(pipeline.open(assets, artifacts), "failed to reopen the asset pipeline") ||
            !require(pipeline.engineAssets().size() == 2,
                    "builtin assets were not registered after reopen") ||
            !require(pipeline.checkIntegrity().succeeded(),
                    "builtin artifacts were not regenerated after being deleted") ||
            !require(pipeline.isImported("triangle.obj"),
                    "persisted metadata was not restored after reopen")) {
        return 1;
    }

    // 孤儿回收：删掉源文件，.meta 和 artifact 都该被清掉。
    const size_t metadata_before = countMetadataFiles(assets);
    const size_t artifacts_before = countFiles(artifacts);
    const size_t asset_count = pipeline.sourceAssets("triangle.obj").assets.size();
    std::filesystem::remove(assets / "triangle.obj", error);
    if (!require(!error, "failed to delete the OBJ source")) {
        return 1;
    }
    {
        const auto plan = pipeline.planReconcile();
        if (!require(plan.orphans.size() == asset_count,
                    "deleting the source did not orphan its assets")) {
            return 1;
        }
        const auto report = pipeline.reconcile();
        if (!require(report.succeeded(), "the orphan reconcile reported errors") ||
                !require(report.forgotten == asset_count,
                        "the orphan reconcile did not forget every asset") ||
                !require(countMetadataFiles(assets) < metadata_before,
                        "orphaned .meta files were not removed") ||
                !require(countFiles(artifacts) < artifacts_before,
                        "orphaned artifacts were not removed") ||
                !require(!pipeline.isImported("triangle.obj"),
                        "the forgotten source is still reported as imported")) {
            return 1;
        }
    }

    // Settings 层：三层解析 + 用户设定跨重导入存活 + 非法值回退默认。
    // 必须是真能解码的图片 —— 后面的 reconcile 会真的导入它。
    if (!require(writeBinary(assets / "sprite.tga", onePixelImage()),
                "failed to write the settings fixture")) {
        return 1;
    }
    {
        const auto settings = pipeline.sourceSettings("sprite.tga");
        if (!require(settings.valid, "the texture importer reported no settings") ||
                !require(settings.schema.size() == 1,
                        "the texture importer must declare exactly one setting") ||
                !require(settings.resolved.getString("Colorspace") == "srgb",
                        "Colorspace must default to srgb") ||
                !require(settings.resolved.layerOf("Colorspace") ==
                                arti::asset::SettingLayer::Default,
                        "an untouched setting must resolve from the Default layer")) {
            return 1;
        }
    }

    // 非法的 Authored 值：回退默认并报 issue，不当成损坏。
    {
        arti::asset::AssetSettings bad;
        bad.authored["Colorspace"] = std::string{ "bogus" };
        bad.authored["NotASetting"] = true;
        const auto resolved = arti::asset::resolveSettings(
                pipeline.sourceSettings("sprite.tga").schema, bad);
        if (!require(resolved.getString("Colorspace") == "srgb",
                    "an invalid authored value must fall back to the default") ||
                !require(resolved.layerOf("Colorspace") == arti::asset::SettingLayer::Default,
                        "an invalid authored value must not claim the Authored layer") ||
                !require(resolved.issues().size() == 2,
                        "both the invalid value and the unknown key must be reported")) {
            return 1;
        }
    }

    // 逐键解析：Authored 只覆盖它指定的那个键，不影响其他层。
    {
        arti::asset::AssetSettings mixed;
        mixed.authored["Colorspace"] = std::string{ "linear" };
        const auto resolved = arti::asset::resolveSettings(
                pipeline.sourceSettings("sprite.tga").schema, mixed);
        if (!require(resolved.getString("Colorspace") == "linear",
                    "an authored value must win over the default") ||
                !require(resolved.layerOf("Colorspace") == arti::asset::SettingLayer::Authored,
                        "an authored value must report the Authored layer")) {
            return 1;
        }
        // 哈希对设置变化敏感 —— 变更检测将来靠它。
        const auto baseline = arti::asset::resolveSettings(
                pipeline.sourceSettings("sprite.tga").schema, {});
        if (!require(resolved.hash() != baseline.hash(),
                    "the resolved hash must change when a setting changes")) {
            return 1;
        }
    }

    // Inferred 层：低于 Authored、高于 Default。
    {
        arti::asset::AssetSettings inferred_only;
        inferred_only.inferred["Colorspace"] = { std::string{ "linear" },
            "Model/foo.gltf", "normal_texture" };
        const auto resolved = arti::asset::resolveSettings(
                pipeline.sourceSettings("sprite.tga").schema, inferred_only);
        if (!require(resolved.getString("Colorspace") == "linear",
                    "an inferred value must win over the default") ||
                !require(resolved.layerOf("Colorspace") == arti::asset::SettingLayer::Inferred,
                        "an inferred value must report the Inferred layer")) {
            return 1;
        }

        arti::asset::AssetSettings both = inferred_only;
        both.authored["Colorspace"] = std::string{ "srgb" };
        const auto overridden = arti::asset::resolveSettings(
                pipeline.sourceSettings("sprite.tga").schema, both);
        if (!require(overridden.getString("Colorspace") == "srgb",
                    "an authored value must win over an inferred value")) {
            return 1;
        }
    }

    // 阶段 3：容器推断 + 拓扑序 + 去重。
    std::filesystem::create_directories(assets / "Model", error);
    if (!require(!error, "failed to create the model fixture directory") ||
            !require(writeBinary(assets / "Model" / "surface.tga", onePixelImage()),
                    "failed to write the image fixture") ||
            !require(writeText(assets / "Model" / "helmet.gltf",
                            minimalGltf("surface.tga", "normal")),
                    "failed to write the gltf fixture")) {
        return 1;
    }

    {
        const auto plan = pipeline.planReconcile();
        if (!require(plan.complete(), "the inference plan did not complete")) {
            return 1;
        }

        // 拓扑序：被引用的贴图必须排在引用它的 glTF 之前。
        // 字母序恰好是相反的（helmet.gltf < surface.png），所以这条断言有意义。
        size_t png_at = plan.items.size();
        size_t gltf_at = plan.items.size();
        for (size_t index = 0; index < plan.items.size(); ++index) {
            const auto path = plan.items[index].source_path.generic_string();
            if (path == "Model/surface.tga") {
                png_at = index;
            } else if (path == "Model/helmet.gltf") {
                gltf_at = index;
            }
        }
        if (!require(png_at < gltf_at,
                    "the referenced texture must be ordered before the glTF that uses it")) {
            return 1;
        }

        // 推断：glTF 把 normalTexture 槽位翻译成 Colorspace=linear。
        const auto png_item = std::ranges::find_if(plan.items, [](const auto& item) {
            return item.source_path.generic_string() == "Model/surface.tga";
        });
        if (!require(png_item != plan.items.end(), "the image is missing from the plan")) {
            return 1;
        }
        const auto inferred = png_item->inferred.find("Colorspace");
        if (!require(inferred != png_item->inferred.end(),
                    "the glTF did not publish a Colorspace inference") ||
                !require(std::get<std::string>(inferred->second.value) == "linear",
                        "a normal-map slot must infer linear") ||
                !require(inferred->second.usage == "normal",
                        "the inference must record which slot produced it")) {
            return 1;
        }
    }

    const auto inferred_report = pipeline.reconcile();
    if (!require(inferred_report.succeeded(), "the inference reconcile failed")) {
        return 1;
    }
    {
        // 推断生效：贴图按 linear 编码，且 layer 是 Inferred 而不是 Default。
        const auto settings = pipeline.sourceSettings("Model/surface.tga");
        if (!require(settings.resolved.getString("Colorspace") == "linear",
                    "the inferred Colorspace did not reach the texture importer") ||
                !require(settings.resolved.layerOf("Colorspace") ==
                                arti::asset::SettingLayer::Inferred,
                        "the value must resolve from the Inferred layer")) {
            return 1;
        }

        // 去重：glTF 引用外部贴图的产出，不再自己产出 texture 子资产。
        const auto gltf = pipeline.sourceAssets("Model/helmet.gltf");
        for (const auto& asset: gltf.assets) {
            if (!require(asset.type != arti::engine::asset::kTextureAssetType,
                        "the glTF must reference the standalone texture instead of "
                        "decoding its own copy")) {
                return 1;
            }
        }

        // 材质依赖必须指向 standalone 纹理的 handle。
        const auto png = pipeline.sourceAssets("Model/surface.tga");
        if (!require(png.assets.size() == 1, "the image must produce exactly one asset")) {
            return 1;
        }
        const arti::core::UUID texture_handle = png.assets.front().handle;
        bool material_found = false;
        for (const auto& asset: gltf.assets) {
            if (asset.type != arti::engine::asset::kMaterialAssetType) {
                continue;
            }
            material_found = true;
            if (!require(std::ranges::find(asset.dependencies, texture_handle) !=
                                asset.dependencies.end(),
                        "the glTF material must depend on the standalone texture")) {
                return 1;
            }
        }
        if (!require(material_found, "the glTF did not produce a material")) {
            return 1;
        }
    }

    // 材质必须持住它的纹理：AssetManager::m_loaded 是 weak_ptr，纹理又跨源
    // 共享，所以只有材质自己持引用才能保证 UUID 指向的资产还活着。
    {
        const auto gltf = pipeline.sourceAssets("Model/helmet.gltf");
        arti::core::UUID material_handle;
        for (const auto& asset: gltf.assets) {
            if (asset.type == arti::engine::asset::kMaterialAssetType) {
                material_handle = asset.handle;
            }
        }
        const auto png = pipeline.sourceAssets("Model/surface.tga");
        if (!require(material_handle.isValid() && png.assets.size() == 1,
                    "the lifetime fixture is missing its material or texture")) {
            return 1;
        }
        const arti::core::UUID texture_handle = png.assets.front().handle;

        auto material = pipeline.manager().load(material_handle);
        if (!require(material != nullptr, "the material failed to load")) {
            return 1;
        }
        // 不持任何纹理的 shared_ptr，然后看它是否仍然存活。
        if (!require(pipeline.manager().getAsset(texture_handle) != nullptr,
                    "a loaded material must keep its textures alive")) {
            return 1;
        }
        material.reset();
        if (!require(pipeline.manager().getAsset(texture_handle) == nullptr,
                    "dropping the material must release its textures")) {
            return 1;
        }
    }

    // OBJ 走同一条路：mtl 里的 map_Bump 推断出 linear，且不再自己解码贴图。
    std::filesystem::create_directories(assets / "Obj", error);
    if (!require(!error, "failed to create the obj fixture directory") ||
            !require(writeBinary(assets / "Obj" / "rock.tga", onePixelImage()),
                    "failed to write the obj texture fixture") ||
            !require(writeText(assets / "Obj" / "rock.mtl",
                            "newmtl stone\nmap_Bump rock.tga\n"),
                    "failed to write the mtl fixture") ||
            !require(writeText(assets / "Obj" / "rock.obj",
                            std::string{ "mtllib rock.mtl\nusemtl stone\n" } +
                                    std::string{ kTriangle }),
                    "failed to write the obj fixture")) {
        return 1;
    }
    {
        const auto plan = pipeline.planReconcile();
        const auto tga = std::ranges::find_if(plan.items, [](const auto& item) {
            return item.source_path.generic_string() == "Obj/rock.tga";
        });
        if (!require(tga != plan.items.end(), "the obj texture is missing from the plan")) {
            return 1;
        }
        const auto inferred = tga->inferred.find("Colorspace");
        if (!require(inferred != tga->inferred.end(),
                    "the obj did not publish a Colorspace inference") ||
                !require(std::get<std::string>(inferred->second.value) == "linear",
                        "an mtl bump slot must infer linear") ||
                !require(inferred->second.usage == "normal",
                        "the mtl inference must record its slot")) {
            return 1;
        }

        size_t tga_at = plan.items.size();
        size_t obj_at = plan.items.size();
        for (size_t index = 0; index < plan.items.size(); ++index) {
            const auto path = plan.items[index].source_path.generic_string();
            if (path == "Obj/rock.tga") {
                tga_at = index;
            } else if (path == "Obj/rock.obj") {
                obj_at = index;
            }
        }
        if (!require(tga_at < obj_at,
                    "the mtl texture must be ordered before the obj that uses it")) {
            return 1;
        }
    }
    if (!require(pipeline.reconcile().succeeded(), "the obj reconcile failed")) {
        return 1;
    }
    {
        const auto obj = pipeline.sourceAssets("Obj/rock.obj");
        for (const auto& asset: obj.assets) {
            if (!require(asset.type != arti::engine::asset::kTextureAssetType,
                        "the obj must reference the standalone texture instead of "
                        "decoding its own copy")) {
                return 1;
            }
        }
        const auto settings = pipeline.sourceSettings("Obj/rock.tga");
        if (!require(settings.resolved.getString("Colorspace") == "linear",
                    "the mtl inference did not reach the texture importer")) {
            return 1;
        }
    }

    // 推断冲突：第二个 glTF 把同一张图当 baseColor 用（→ srgb）。
    // 一个源文件只有一份设置，所以只能有一个胜出，并且必须被报告。
    if (!require(writeText(assets / "Model" / "armor.gltf",
                        minimalGltf("surface.tga", "base_color")),
                "failed to write the conflicting gltf fixture")) {
        return 1;
    }
    {
        const auto plan = pipeline.planReconcile();
        if (!require(plan.inference_conflicts.size() == 1,
                    "a contradictory inference must be reported exactly once")) {
            return 1;
        }
        const auto& conflict = plan.inference_conflicts.front();
        // 裁决按发布者路径字典序：armor.gltf < helmet.gltf。
        if (!require(conflict.winner.generic_string() == "Model/armor.gltf",
                    "the conflict must be arbitrated deterministically by publisher path") ||
                !require(conflict.key == "Colorspace",
                        "the conflict must name the contested setting")) {
            return 1;
        }
    }

    // .artimaterial：编辑器创作的材质是真实源文件，走普通 Root 资产那条路。
    std::filesystem::create_directories(assets / "Materials", error);
    if (!require(!error, "failed to create the materials directory") ||
            !require(writeBinary(assets / "Materials" / "surface_n.tga", onePixelImage()),
                    "failed to write the material texture fixture") ||
            !require(writeText(assets / "Materials" / "stone.artimaterial",
                            "Type: PBR\nBaseColor: [1, 1, 1, 1]\nRoughness: 0.25\n"
                            "NormalTexture: Materials/surface_n.tga\n"),
                    "failed to write the artimaterial fixture")) {
        return 1;
    }
    {
        const auto plan = pipeline.planReconcile();
        // 贴图必须排在引用它的材质之前。
        size_t tex_at = plan.items.size();
        size_t mat_at = plan.items.size();
        for (size_t index = 0; index < plan.items.size(); ++index) {
            const auto path = plan.items[index].source_path.generic_string();
            if (path == "Materials/surface_n.tga") {
                tex_at = index;
            } else if (path == "Materials/stone.artimaterial") {
                mat_at = index;
            }
        }
        if (!require(tex_at < mat_at,
                    "the texture must be ordered before the material that references it")) {
            return 1;
        }
    }
    if (!require(pipeline.reconcile().succeeded(), "the artimaterial reconcile failed")) {
        return 1;
    }
    {
        const auto material = pipeline.sourceAssets("Materials/stone.artimaterial");
        if (!require(material.assets.size() == 1,
                    "an artimaterial must produce exactly one material asset") ||
                !require(material.assets.front().type ==
                                arti::engine::asset::kMaterialAssetType,
                        "the artimaterial produced the wrong asset type")) {
            return 1;
        }

        // 源文件里的路径引用必须解析成纹理的 UUID 依赖。
        const auto texture = pipeline.sourceAssets("Materials/surface_n.tga");
        if (!require(texture.assets.size() == 1, "the material texture is missing")) {
            return 1;
        }
        if (!require(std::ranges::find(material.assets.front().dependencies,
                            texture.assets.front().handle) !=
                            material.assets.front().dependencies.end(),
                    "the path reference must resolve to the texture's UUID")) {
            return 1;
        }

        // 材质也发布推断：NormalTexture 槽位 → linear。
        const auto settings = pipeline.sourceSettings("Materials/surface_n.tga");
        if (!require(settings.resolved.getString("Colorspace") == "linear",
                    "the material's slot inference did not reach the texture") ||
                !require(settings.resolved.layerOf("Colorspace") ==
                                arti::asset::SettingLayer::Inferred,
                        "the material inference must resolve from the Inferred layer")) {
            return 1;
        }
    }

    // 已知缺口：源文件内容变更还没有检测（排在多线程之后），所以改了
    // .artimaterial 之后 reconcile 会报 current，必须显式重导入。
    // 这条断言把现状钉住 —— 等 ContentHash 开始被读取时它会失败，提醒更新。
    {
        if (!require(writeText(assets / "Materials" / "stone.artimaterial",
                            "Type: PBR\nBaseColor: [1, 1, 1, 1]\nRoughness: 0.9\n"
                            "NormalTexture: Materials/surface_n.tga\n"),
                    "failed to rewrite the artimaterial fixture")) {
            return 1;
        }
        const auto report = pipeline.reconcile();
        if (!require(report.reimported == 0,
                    "source-content change detection is not implemented yet; if this now "
                    "reimports, update this assertion and the deferred-work notes")) {
            return 1;
        }
        // 显式导入是当前唯一的出口，编辑器保存材质后必须走它。
        if (!require(pipeline.importFile("Materials/stone.artimaterial").succeeded(),
                    "an explicit import must pick up the edited source")) {
            return 1;
        }
    }

    // Extract：派生材质 → 独立 Root 源文件，prefab 引用改指向提取物。
    {
        const auto gltf = pipeline.sourceAssets("Model/helmet.gltf");
        arti::core::UUID derived;
        arti::core::UUID prefab;
        for (const auto& asset: gltf.assets) {
            if (asset.type == arti::engine::asset::kMaterialAssetType) {
                derived = asset.handle;
            } else if (asset.type == arti::engine::asset::kPrefabAssetType) {
                prefab = asset.handle;
            }
        }
        if (!require(derived.isValid() && prefab.isValid(),
                    "the extract fixture is missing its material or prefab")) {
            return 1;
        }

        const auto extracted = pipeline.extractMaterial(derived, "Model/helmet_mat.artimaterial");
        if (!require(extracted.succeeded, "extract failed: " + extracted.error) ||
                !require(extracted.handle.isValid() && extracted.handle != derived,
                        "the extracted material must be a new asset")) {
            return 1;
        }

        // 提取物是独立 Root 资产：local_id 为空，自己一份 sidecar。
        const auto standalone = pipeline.sourceAssets("Model/helmet_mat.artimaterial");
        if (!require(standalone.assets.size() == 1 &&
                            standalone.assets.front().local_id.empty(),
                    "the extracted material must be a standalone Root asset")) {
            return 1;
        }

        // 容器不再产出那份派生材质。
        for (const auto& asset: pipeline.sourceAssets("Model/helmet.gltf").assets) {
            if (!require(asset.type != arti::engine::asset::kMaterialAssetType,
                        "the container must stop producing the extracted material")) {
                return 1;
            }
        }

        // prefab 依赖必须指向提取物 —— 这是 Extract 的全部意义。
        const auto prefab_entry = pipeline.manager().catalog().find(prefab);
        if (!require(prefab_entry.has_value(), "the prefab vanished after extract") ||
                !require(std::ranges::find(prefab_entry->dependencies, extracted.handle) !=
                                prefab_entry->dependencies.end(),
                        "the prefab must depend on the extracted material")) {
            return 1;
        }

        // 覆盖必须活过 reconcile，否则重导入就会退回容器自己那份。
        if (!require(pipeline.reconcile().succeeded(), "the post-extract reconcile failed")) {
            return 1;
        }
        const auto after = pipeline.manager().catalog().find(prefab);
        if (!require(after.has_value() &&
                            std::ranges::find(after->dependencies, extracted.handle) !=
                                    after->dependencies.end(),
                    "the extraction override must survive a reconcile")) {
            return 1;
        }

        // 目标已存在时必须拒绝，不能静默覆盖用户文件。
        const auto again = pipeline.extractMaterial(derived, "Model/helmet_mat.artimaterial");
        if (!require(!again.succeeded, "extract must refuse to overwrite an existing file")) {
            return 1;
        }
    }

    // 坏 .meta 不该让项目打不开。
    if (!require(writeText(assets / "broken.obj.meta", "not: valid: metadata: at: all\n"),
                "failed to write the broken metadata fixture")) {
        return 1;
    }
    pipeline.close();
    if (!require(pipeline.open(assets, artifacts),
                "a malformed .meta prevented the workspace from opening")) {
        return 1;
    }
    {
        const auto plan = pipeline.planReconcile();
        if (!require(plan.metadata_issues.size() == 1,
                    "the malformed .meta was not reported as an issue")) {
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
