#include "asset/importers/detail/gltf_parser.h"

#include <cgltf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>

namespace arti::engine::asset::detail {
namespace {

// 退化判据。用于法线/切线的长度、UV 的行列式、节点矩阵的行列式。
constexpr float kDegenerateEpsilon = 1e-8f;

struct CgltfDeleter {
    void operator()(cgltf_data* data) const noexcept { cgltf_free(data); }
};
using CgltfData = std::unique_ptr<cgltf_data, CgltfDeleter>;

std::string_view resultName(cgltf_result result) {
    switch (result) {
        case cgltf_result_success: return "success";
        case cgltf_result_data_too_short: return "data too short";
        case cgltf_result_unknown_format: return "unknown format";
        case cgltf_result_invalid_json: return "invalid JSON";
        case cgltf_result_invalid_gltf: return "invalid glTF";
        case cgltf_result_invalid_options: return "invalid options";
        case cgltf_result_file_not_found: return "file not found";
        case cgltf_result_io_error: return "I/O error";
        case cgltf_result_out_of_memory: return "out of memory";
        case cgltf_result_legacy_gltf: return "legacy glTF (1.0 is not supported)";
        default: return "unknown error";
    }
}

[[noreturn]] void fail(std::string_view what, const std::filesystem::path& file,
        cgltf_result result) {
    throw std::runtime_error("Failed to " + std::string{ what } + " '" + file.string() + "': " +
                             std::string{ resultName(result) } + ".");
}

CgltfData openDocument(const std::filesystem::path& file) {
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    const std::string name = file.string();

    cgltf_result result = cgltf_parse_file(&options, name.c_str(), &raw);
    if (result != cgltf_result_success) {
        fail("parse", file, result);
    }
    CgltfData data{ raw };

    // buffer 一并加载：外部 .bin、GLB 的内嵌块、data URI 三种形态都由它统一处理，
    // 后面读 accessor 时就不用再关心数据从哪来。
    result = cgltf_load_buffers(&options, data.get(), name.c_str());
    if (result != cgltf_result_success) {
        fail("load buffers for", file, result);
    }
    result = cgltf_validate(data.get());
    if (result != cgltf_result_success) {
        fail("validate", file, result);
    }
    return data;
}

// 子资产 suffix 会拼进 source_path，而 .meta 是按 source_path 落盘的 —— 键里带路径分隔符
// 会让 .meta 跑到子目录里去。所以统一替换掉。
std::string sanitizeKey(std::string key) {
    std::ranges::replace(key, '/', '_');
    std::ranges::replace(key, '\\', '_');
    return key;
}

// data URI 里 base64 载荷的解码。cgltf 只自动解 buffer 的 data URI，image 的不解 ——
// image 那条它把整个 uri 字符串留给调用方。
std::vector<std::byte> decodeDataUri(const char* uri) {
    const std::string_view text{ uri };
    const auto comma = text.find(',');
    if (comma == std::string_view::npos || text.find(";base64") == std::string_view::npos) {
        // 非 base64 的 data URI（百分号编码的纯文本）在图片里不会出现，不值得支持。
        throw std::runtime_error("Only base64 data URIs are supported for glTF images.");
    }
    const std::string_view payload = text.substr(comma + 1);
    size_t size = payload.size() / 4 * 3;
    if (payload.ends_with("==")) {
        size -= 2;
    } else if (payload.ends_with("=")) {
        size -= 1;
    }

    cgltf_options options{};
    void* decoded = nullptr;
    const cgltf_result result =
            cgltf_load_buffer_base64(&options, size, payload.data(), &decoded);
    if (result != cgltf_result_success || decoded == nullptr) {
        throw std::runtime_error("Failed to decode a base64 data URI in the glTF.");
    }
    std::vector<std::byte> bytes(size);
    std::memcpy(bytes.data(), decoded, size);
    ::free(decoded);
    return bytes;
}

// 收集所有图片，按来源去重。返回值的下标就是 GltfMaterial 里那些 *_image 字段的取值。
//
// 三种来源：外部 URI、GLB 的 buffer view、base64 data URI。cgltf 已经把 buffer 都加载好了，
// 所以后两种直接从内存拿。
std::vector<GltfImage> collectImages(const cgltf_data& data,
        const std::filesystem::path& base_directory) {
    std::vector<GltfImage> images;
    images.reserve(static_cast<size_t>(data.images_count));

    for (cgltf_size index = 0; index < data.images_count; ++index) {
        const cgltf_image& source = data.images[index];
        GltfImage image;

        if (source.uri != nullptr && source.uri[0] != '\0') {
            if (std::string_view{ source.uri }.starts_with("data:")) {
                image.key = sanitizeKey("image." + std::to_string(index));
                image.bytes = decodeDataUri(source.uri);
            } else {
                // URI 是百分号编码的，要先解开才是真的文件名。
                std::string decoded{ source.uri };
                decoded.resize(cgltf_decode_uri(decoded.data()));
                const std::filesystem::path relative{ decoded };
                image.key = sanitizeKey(relative.filename().string());
                image.file = (base_directory / relative).lexically_normal();
            }
        } else if (source.buffer_view != nullptr) {
            // GLB 的内嵌图片：数据在某个 buffer 的一段区间里。
            const cgltf_buffer_view& view = *source.buffer_view;
            if (view.buffer == nullptr || view.buffer->data == nullptr) {
                throw std::runtime_error("A glTF image references an unloaded buffer.");
            }
            const auto* bytes = static_cast<const std::byte*>(view.buffer->data) + view.offset;
            image.key = sanitizeKey("image." + std::to_string(index));
            image.bytes.assign(bytes, bytes + view.size);
        } else {
            // 既没 URI 也没 buffer view 的 image 是合法但空的，跳过它 ——
            // 引用它的材质槽会退化成纯色。
            continue;
        }

        // 同一个文件被多个 image 条目引用时只留一份。glTF 里这种情况不算少见
        // （不同 sampler 配同一张图），而重复导入等于把几 MB 的贴图存两遍。
        const auto existing = std::ranges::find_if(images, [&image](const GltfImage& other) {
            return !image.file.empty() ? other.file == image.file : other.key == image.key;
        });
        if (existing != images.end()) {
            continue;
        }
        images.push_back(std::move(image));
    }
    return images;
}

// 某个纹理槽引用的图片在 images 里的下标。-1 表示槽位为空。
int imageIndexOf(const cgltf_data& data, const std::vector<GltfImage>& images,
        const std::filesystem::path& base_directory, const cgltf_texture_view& view) {
    if (view.texture == nullptr) {
        return -1;
    }
    const cgltf_image* image = view.texture->image;
    // basisu / webp 扩展把图片挂在别的字段上。我们不解码 KTX2，但至少 webp 能走 stb。
    if (image == nullptr && view.texture->has_webp) {
        image = view.texture->webp_image;
    }
    if (image == nullptr) {
        return -1;
    }

    // 用和 collectImages 完全一样的规则算出这张图的身份，再去列表里找。
    std::filesystem::path file;
    std::string key;
    if (image->uri != nullptr && image->uri[0] != '\0' &&
            !std::string_view{ image->uri }.starts_with("data:")) {
        std::string decoded{ image->uri };
        decoded.resize(cgltf_decode_uri(decoded.data()));
        const std::filesystem::path relative{ decoded };
        file = (base_directory / relative).lexically_normal();
        key = sanitizeKey(relative.filename().string());
    } else {
        key = sanitizeKey("image." + std::to_string(cgltf_image_index(&data, image)));
    }

    const auto found = std::ranges::find_if(images, [&](const GltfImage& candidate) {
        return !file.empty() ? candidate.file == file : candidate.key == key;
    });
    return found == images.end() ? -1 : static_cast<int>(found - images.begin());
}

std::string materialName(const cgltf_data& data, const cgltf_material* material) {
    if (material == nullptr) {
        return "Default";
    }
    if (material->name != nullptr && material->name[0] != '\0') {
        return material->name;
    }
    return "material." + std::to_string(cgltf_material_index(&data, material));
}

std::vector<GltfMaterial> extractMaterials(const cgltf_data& data,
        const std::vector<GltfImage>& images, const std::filesystem::path& base_directory) {
    std::vector<GltfMaterial> materials;
    materials.reserve(static_cast<size_t>(data.materials_count));

    for (cgltf_size index = 0; index < data.materials_count; ++index) {
        const cgltf_material& source = data.materials[index];
        GltfMaterial material;
        material.name = materialName(data, &source);

        const auto slot = [&](const cgltf_texture_view& view) {
            return imageIndexOf(data, images, base_directory, view);
        };

        if (source.has_pbr_metallic_roughness) {
            const auto& pbr = source.pbr_metallic_roughness;
            material.base_color = { pbr.base_color_factor[0], pbr.base_color_factor[1],
                pbr.base_color_factor[2], pbr.base_color_factor[3] };
            material.metallic = pbr.metallic_factor;
            material.roughness = pbr.roughness_factor;
            material.base_color_image = slot(pbr.base_color_texture);
            material.metallic_roughness_image = slot(pbr.metallic_roughness_texture);
        } else if (source.has_pbr_specular_glossiness) {
            // KHR_materials_pbrSpecularGlossiness 已废弃。近似成 metallic-roughness：
            // diffuse 当 base color，glossiness 反过来当 roughness，metallic 归 0。
            // 不精确，但比整个材质变默认白色好。
            const auto& sg = source.pbr_specular_glossiness;
            material.base_color = { sg.diffuse_factor[0], sg.diffuse_factor[1],
                sg.diffuse_factor[2], sg.diffuse_factor[3] };
            material.metallic = 0.0f;
            material.roughness = 1.0f - sg.glossiness_factor;
            material.base_color_image = slot(sg.diffuse_texture);
        }

        material.normal_image = slot(source.normal_texture);
        material.occlusion_image = slot(source.occlusion_texture);
        material.emissive_image = slot(source.emissive_texture);
        // occlusionTexture.scale 在 glTF 里就是 AO 的强度。
        material.occlusion = source.occlusion_texture.texture != nullptr
                ? source.occlusion_texture.scale
                : 1.0f;

        // **有损映射**：rendering::Material 的 emissive 只有一个标量强度，没有颜色。
        // 这里取 emissiveFactor 的最大分量当强度，颜色的部分只能由 emissive 贴图带。
        // 纯色自发光（有 factor 无贴图）因此会丢掉色相 —— 真要修得给 Material 加
        // emissive_color 字段，那是渲染端的改动。
        const float emissive_peak = std::max({ source.emissive_factor[0],
            source.emissive_factor[1], source.emissive_factor[2] });
        const float emissive_strength =
                source.has_emissive_strength ? source.emissive_strength.emissive_strength : 1.0f;
        material.emissive = emissive_peak * emissive_strength;

        materials.push_back(std::move(material));
    }
    return materials;
}

glm::vec2 readVec2(const cgltf_accessor& accessor, cgltf_size index) {
    std::array<cgltf_float, 2> value{};
    if (!cgltf_accessor_read_float(&accessor, index, value.data(), value.size())) {
        throw std::runtime_error("Failed to read a glTF vec2 accessor.");
    }
    return { value[0], value[1] };
}

glm::vec3 readVec3(const cgltf_accessor& accessor, cgltf_size index) {
    std::array<cgltf_float, 3> value{};
    if (!cgltf_accessor_read_float(&accessor, index, value.data(), value.size())) {
        throw std::runtime_error("Failed to read a glTF vec3 accessor.");
    }
    return { value[0], value[1], value[2] };
}

glm::vec4 readVec4(const cgltf_accessor& accessor, cgltf_size index) {
    std::array<cgltf_float, 4> value{};
    if (!cgltf_accessor_read_float(&accessor, index, value.data(), value.size())) {
        throw std::runtime_error("Failed to read a glTF vec4 accessor.");
    }
    return { value[0], value[1], value[2], value[3] };
}

// 从法线现造一条切线。UV 退化（三角形在 UV 空间面积为 0）时的兜底。
glm::vec3 fallbackTangent(const glm::vec3& normal) {
    const glm::vec3 axis = std::fabs(normal.z) < 0.999f ? glm::vec3{ 0.0f, 0.0f, 1.0f }
                                                       : glm::vec3{ 0.0f, 1.0f, 0.0f };
    return glm::normalize(glm::cross(axis, normal));
}

void checkAccessor(const cgltf_accessor* accessor, cgltf_type type, cgltf_size count,
        std::string_view semantic) {
    if (accessor->type != type || accessor->count != count) {
        throw std::runtime_error("The glTF primitive has an invalid " + std::string{ semantic } +
                                 " accessor.");
    }
}

// 一个 primitive 追加成一段 submesh。顶点保持局部空间 —— 变换属于 PrefabNode。
void appendPrimitive(GltfMesh& mesh, const cgltf_data& data, const cgltf_primitive& primitive) {
    if (primitive.type != cgltf_primitive_type_triangles) {
        throw std::runtime_error("Only triangle-list glTF primitives are supported.");
    }
    if (primitive.has_draco_mesh_compression) {
        throw std::runtime_error("KHR_draco_mesh_compression is not supported.");
    }

    const cgltf_accessor* positions =
            cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
    if (positions == nullptr || positions->type != cgltf_type_vec3 || positions->count == 0) {
        throw std::runtime_error("The glTF primitive has no valid POSITION accessor.");
    }
    const cgltf_size vertex_count = positions->count;
    if (vertex_count > std::numeric_limits<uint32_t>::max() ||
            mesh.vertices.size() > std::numeric_limits<uint32_t>::max() - vertex_count) {
        throw std::runtime_error("The glTF mesh exceeds the 32-bit vertex limit.");
    }

    const cgltf_accessor* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
    const cgltf_accessor* texcoords =
            cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
    const cgltf_accessor* tangents =
            cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0);
    if (normals != nullptr) {
        checkAccessor(normals, cgltf_type_vec3, vertex_count, "NORMAL");
    }
    if (texcoords != nullptr) {
        checkAccessor(texcoords, cgltf_type_vec2, vertex_count, "TEXCOORD_0");
    }
    if (tangents != nullptr) {
        checkAccessor(tangents, cgltf_type_vec4, vertex_count, "TANGENT");
    }

    const uint32_t vertex_offset = static_cast<uint32_t>(mesh.vertices.size());
    const size_t count = static_cast<size_t>(vertex_count);
    // 哪些顶点的法线/切线是文件里带的。没带的要从三角形累加算出来，而带了的不能被覆盖。
    std::vector<bool> has_normal(count, false);
    std::vector<bool> has_tangent(count, false);
    std::vector<glm::vec3> normal_accum(count, glm::vec3{ 0.0f });
    std::vector<glm::vec3> tangent_accum(count, glm::vec3{ 0.0f });
    std::vector<glm::vec3> bitangent_accum(count, glm::vec3{ 0.0f });
    // glTF 的 TANGENT 是 vec4，w 是副切线的手性。我们的 MeshVertex 存显式 bitangent，
    // 所以 w 只在最后合成 bitangent 时用一次，不进顶点。
    std::vector<float> handedness(count, 1.0f);

    mesh.vertices.reserve(mesh.vertices.size() + count);
    for (cgltf_size index = 0; index < vertex_count; ++index) {
        rendering::MeshVertex vertex;
        vertex.position = readVec3(*positions, index);
        if (normals != nullptr) {
            const glm::vec3 normal = readVec3(*normals, index);
            if (glm::length(normal) > kDegenerateEpsilon) {
                vertex.normal = glm::normalize(normal);
                has_normal[static_cast<size_t>(index)] = true;
            }
        }
        if (texcoords != nullptr) {
            // glTF 的 UV 原点已经在左上，和我们的约定一致 —— 不像 OBJ 需要翻 V。
            vertex.uv = readVec2(*texcoords, index);
        }
        if (tangents != nullptr) {
            const glm::vec4 source = readVec4(*tangents, index);
            const glm::vec3 tangent{ source };
            if (glm::length(tangent) > kDegenerateEpsilon) {
                vertex.tangent = glm::normalize(tangent);
                handedness[static_cast<size_t>(index)] = source.w < 0.0f ? -1.0f : 1.0f;
                has_tangent[static_cast<size_t>(index)] = true;
            }
        }
        mesh.vertices.push_back(vertex);
    }

    // 索引。没有 indices 的 primitive 是顺序三角形列表。
    const cgltf_size index_count =
            primitive.indices == nullptr ? vertex_count : primitive.indices->count;
    if (index_count == 0 || index_count % 3 != 0 ||
            index_count > std::numeric_limits<uint32_t>::max() ||
            mesh.indices.size() > std::numeric_limits<uint32_t>::max() - index_count) {
        throw std::runtime_error("The glTF primitive has an invalid triangle index count.");
    }
    if (primitive.indices != nullptr && primitive.indices->type != cgltf_type_scalar) {
        throw std::runtime_error("The glTF primitive has a non-scalar index accessor.");
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(index_count));
    for (cgltf_size index = 0; index < index_count; ++index) {
        const cgltf_size value = primitive.indices == nullptr
                ? index
                : cgltf_accessor_read_index(primitive.indices, index);
        if (value >= vertex_count) {
            throw std::runtime_error("The glTF primitive contains an out-of-range index.");
        }
        indices.push_back(static_cast<uint32_t>(value));
    }

    // 逐三角形累加法线和切线基。切线用标准的 UV 梯度法：把 (edge1, edge2) 在 UV 上的
    // 变化率解出来，得到沿 U 和沿 V 的世界方向。
    for (size_t triangle = 0; triangle + 2 < indices.size(); triangle += 3) {
        const uint32_t i0 = indices[triangle];
        const uint32_t i1 = indices[triangle + 1];
        const uint32_t i2 = indices[triangle + 2];
        const auto& v0 = mesh.vertices[vertex_offset + i0];
        const auto& v1 = mesh.vertices[vertex_offset + i1];
        const auto& v2 = mesh.vertices[vertex_offset + i2];
        const glm::vec3 edge1 = v1.position - v0.position;
        const glm::vec3 edge2 = v2.position - v0.position;

        glm::vec3 face_normal = glm::cross(edge1, edge2);
        if (glm::length(face_normal) > kDegenerateEpsilon) {
            face_normal = glm::normalize(face_normal);
        }

        glm::vec3 tangent{ 0.0f };
        glm::vec3 bitangent{ 0.0f };
        const glm::vec2 delta_uv1 = v1.uv - v0.uv;
        const glm::vec2 delta_uv2 = v2.uv - v0.uv;
        const float uv_determinant = delta_uv1.x * delta_uv2.y - delta_uv1.y * delta_uv2.x;
        if (std::fabs(uv_determinant) > kDegenerateEpsilon) {
            const float inverse = 1.0f / uv_determinant;
            tangent = (edge1 * delta_uv2.y - edge2 * delta_uv1.y) * inverse;
            bitangent = (edge2 * delta_uv1.x - edge1 * delta_uv2.x) * inverse;
        }

        for (const uint32_t index: { i0, i1, i2 }) {
            if (!has_normal[index]) {
                normal_accum[index] += face_normal;
            }
            if (!has_tangent[index]) {
                tangent_accum[index] += tangent;
                bitangent_accum[index] += bitangent;
            }
        }
    }

    for (uint32_t index = 0; index < static_cast<uint32_t>(vertex_count); ++index) {
        auto& vertex = mesh.vertices[vertex_offset + index];
        if (!has_normal[index]) {
            vertex.normal = glm::length(normal_accum[index]) > kDegenerateEpsilon
                    ? glm::normalize(normal_accum[index])
                    : glm::vec3{ 0.0f, 0.0f, 1.0f };
        }

        // Gram-Schmidt：切线要正交于最终的法线，否则法线贴图会有剪切。
        glm::vec3 tangent = has_tangent[index] ? vertex.tangent : tangent_accum[index];
        tangent -= vertex.normal * glm::dot(vertex.normal, tangent);
        tangent = glm::length(tangent) > kDegenerateEpsilon ? glm::normalize(tangent)
                                                           : fallbackTangent(vertex.normal);

        float sign = handedness[index];
        if (!has_tangent[index]) {
            // 文件没给手性时从累加出来的副切线反推：cross(N, T) 和它同向就是 +1。
            sign = glm::dot(glm::cross(vertex.normal, tangent), bitangent_accum[index]) < 0.0f
                    ? -1.0f
                    : 1.0f;
        }
        vertex.tangent = tangent;
        vertex.bitangent = glm::cross(vertex.normal, tangent) * sign;
    }

    rendering::Submesh submesh;
    submesh.index_offset = static_cast<uint32_t>(mesh.indices.size());
    submesh.index_count = static_cast<uint32_t>(indices.size());
    // vertex_offset / vertex_count 描述的是整个网格的顶点缓冲，不是这一段的 ——
    // 和 ObjImporter 的约定一致。vertex_count 要等所有 primitive 都进来才知道，
    // 所以在 parseMesh 的末尾统一回填。
    submesh.vertex_offset = 0;
    submesh.vertex_count = 0;
    submesh.material_index = static_cast<uint32_t>(mesh.submeshes.size());

    // 索引是相对整个 mesh 的顶点缓冲的，所以要加上这个 primitive 的起始偏移。
    // 不用 Submesh::vertex_offset 表达：drawIndexed 的 StartVertexLocation 对所有
    // submesh 是同一个基址（0），逐段偏移只能烘进索引里。
    for (const uint32_t index: indices) {
        mesh.indices.push_back(index + vertex_offset);
    }

    mesh.submeshes.push_back(submesh);
    mesh.material_indices.push_back(primitive.material == nullptr
                    ? -1
                    : static_cast<int>(cgltf_material_index(&data, primitive.material)));
    mesh.material_slots.push_back(materialName(data, primitive.material));
}

GltfMesh parseMesh(const cgltf_data& data, const cgltf_mesh& source, size_t mesh_index) {
    GltfMesh mesh;
    mesh.name = source.name != nullptr && source.name[0] != '\0'
            ? source.name
            : "mesh." + std::to_string(mesh_index);
    for (cgltf_size index = 0; index < source.primitives_count; ++index) {
        appendPrimitive(mesh, data, source.primitives[index]);
    }
    // 回填：所有 submesh 共用同一个顶点缓冲，所以 vertex_count 是整个网格的。
    const auto total = static_cast<uint32_t>(mesh.vertices.size());
    for (auto& submesh: mesh.submeshes) {
        submesh.vertex_count = total;
    }
    return mesh;
}

// 节点树按**前序**展开：先 push 自己拿到下标，再拿那个下标当 parent 递归子节点。
// 于是 parent 的下标必然小于自己，正好满足 PrefabAsset 的拓扑有序要求。
//
// （ArtiEngine-old 的等价函数是后序的，而且它记 children 索引时用的是递归**之前**的
// nodes.size()，那个值指向的是最深的后代而不是那个孩子 —— 是个 bug，没有照搬。）
void collectNodes(std::vector<GltfNode>& nodes, const cgltf_data& data, const cgltf_node& source,
        uint32_t parent) {
    GltfNode node;
    node.name = source.name != nullptr && source.name[0] != '\0'
            ? source.name
            : "node." + std::to_string(cgltf_node_index(&data, &source));
    // cgltf_node_transform_local 统一处理 matrix 和 TRS 两种写法，不用自己判 has_matrix。
    std::array<cgltf_float, 16> local{};
    cgltf_node_transform_local(&source, local.data());
    node.local_transform = glm::make_mat4(local.data());
    node.parent = parent;
    if (source.mesh != nullptr) {
        node.mesh = static_cast<int>(cgltf_mesh_index(&data, source.mesh));
    }

    const auto self = static_cast<uint32_t>(nodes.size());
    nodes.push_back(std::move(node));
    for (cgltf_size index = 0; index < source.children_count; ++index) {
        if (source.children[index] != nullptr) {
            collectNodes(nodes, data, *source.children[index], self);
        }
    }
}

} // namespace

GltfScene parseGltf(const std::filesystem::path& source_file) {
    const CgltfData data = openDocument(source_file);
    const std::filesystem::path base_directory = source_file.parent_path();

    GltfScene scene;
    scene.images = collectImages(*data, base_directory);
    scene.materials = extractMaterials(*data, scene.images, base_directory);

    scene.meshes.reserve(static_cast<size_t>(data->meshes_count));
    for (cgltf_size index = 0; index < data->meshes_count; ++index) {
        GltfMesh mesh = parseMesh(*data, data->meshes[index], index);
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            // 空 mesh 是合法的（比如只带 morph target 的占位）。留一个空槽位保持下标对齐 ——
            // 节点是按 cgltf 的 mesh 序号引用的。
            scene.meshes.push_back(GltfMesh{ .name = std::move(mesh.name) });
            continue;
        }
        scene.meshes.push_back(std::move(mesh));
    }

    // 默认场景优先；没有 scene 就拿所有根节点；连节点都没有就给每个 mesh 造一个。
    // 最后那条是为了不丢几何 —— 有些工具导出的 glTF 只有 meshes 数组。
    const cgltf_scene* source_scene = data->scene;
    if (source_scene == nullptr && data->scenes_count > 0) {
        source_scene = &data->scenes[0];
    }
    if (source_scene != nullptr) {
        for (cgltf_size index = 0; index < source_scene->nodes_count; ++index) {
            if (source_scene->nodes[index] != nullptr) {
                collectNodes(scene.nodes, *data, *source_scene->nodes[index], kNoParentNode);
            }
        }
    } else if (data->nodes_count > 0) {
        for (cgltf_size index = 0; index < data->nodes_count; ++index) {
            if (data->nodes[index].parent == nullptr) {
                collectNodes(scene.nodes, *data, data->nodes[index], kNoParentNode);
            }
        }
    } else {
        for (size_t index = 0; index < scene.meshes.size(); ++index) {
            GltfNode node;
            node.name = scene.meshes[index].name;
            node.mesh = static_cast<int>(index);
            scene.nodes.push_back(std::move(node));
        }
    }

    const bool has_geometry = std::ranges::any_of(scene.meshes,
            [](const GltfMesh& mesh) { return !mesh.vertices.empty(); });
    if (!has_geometry) {
        throw std::runtime_error("The glTF file produced no triangle geometry: " +
                                 source_file.string());
    }
    return scene;
}

} // namespace arti::engine::asset::detail
