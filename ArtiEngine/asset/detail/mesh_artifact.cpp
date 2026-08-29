#include "asset/detail/mesh_artifact.h"

#include <memory>
#include <utility>

namespace arti::engine::asset::detail {
namespace {

constexpr size_t kHeaderSize = 24;

}

std::vector<std::byte> encodeMeshArtifact(const std::vector<rendering::MeshVertex>& vertices,
        const std::vector<uint32_t>& indices, const std::vector<rendering::Submesh>& submeshes,
        const std::vector<std::string>& material_slots) {
    if (vertices.empty() || indices.empty()) {
        throw std::runtime_error("A mesh artifact requires both vertices and indices.");
    }

    std::ostringstream stream{ std::ios::binary };
    stream.write(kMeshArtifactMagic.data(), kMeshArtifactMagic.size());
    writeU32(stream, kMeshArtifactVersion);
    writeU32(stream, static_cast<uint32_t>(vertices.size()));
    writeU32(stream, static_cast<uint32_t>(indices.size()));
    writeU32(stream, static_cast<uint32_t>(submeshes.size()));
    writeU32(stream, static_cast<uint32_t>(material_slots.size()));

    stream.write(reinterpret_cast<const char*>(vertices.data()),
            static_cast<std::streamsize>(vertices.size() * sizeof(rendering::MeshVertex)));
    for (const uint32_t index: indices) {
        writeU32(stream, index);
    }
    stream.write(reinterpret_cast<const char*>(submeshes.data()),
            static_cast<std::streamsize>(submeshes.size() * sizeof(rendering::Submesh)));

    for (const std::string& slot: material_slots) {
        writeU32(stream, static_cast<uint32_t>(slot.size()));
        stream.write(slot.data(), static_cast<std::streamsize>(slot.size()));
    }

    const std::string text = stream.str();
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

std::shared_ptr<MeshAsset> decodeMeshArtifact(core::UUID handle,
        const std::vector<std::byte>& data) {
    if (data.size() < kHeaderSize) {
        throw std::runtime_error("The mesh artifact is truncated.");
    }
    if (std::memcmp(data.data(), kMeshArtifactMagic.data(), kMeshArtifactMagic.size()) != 0) {
        throw std::runtime_error("The mesh artifact has a wrong magic.");
    }
    const uint32_t version = readU32(data, 4);
    if (version != kMeshArtifactVersion) {
        throw std::runtime_error("The mesh artifact version is " + std::to_string(version) +
                                 ", expected " + std::to_string(kMeshArtifactVersion) + ".");
    }

    const auto vertex_count = readU32(data, 8);
    const auto index_count = readU32(data, 12);
    const auto submesh_count = readU32(data, 16);
    const auto slot_count = readU32(data, 20);

    size_t offset = kHeaderSize;
    const auto require = [&data](size_t need, size_t at) {
        if (at + need > data.size()) {
            throw std::runtime_error("The mesh artifact body is truncated.");
        }
    };

    std::vector<rendering::MeshVertex> vertices(vertex_count);
    const size_t vertex_bytes = vertex_count * sizeof(rendering::MeshVertex);
    require(vertex_bytes, offset);
    std::memcpy(vertices.data(), data.data() + offset, vertex_bytes);
    offset += vertex_bytes;

    std::vector<uint32_t> indices(index_count);
    for (uint32_t i = 0; i < index_count; ++i) {
        indices[i] = readU32(data, offset);
        offset += 4;
    }

    std::vector<rendering::Submesh> submeshes(submesh_count);
    const size_t submesh_bytes = submesh_count * sizeof(rendering::Submesh);
    require(submesh_bytes, offset);
    std::memcpy(submeshes.data(), data.data() + offset, submesh_bytes);
    offset += submesh_bytes;

    std::vector<std::string> material_slots;
    material_slots.reserve(slot_count);
    for (uint32_t i = 0; i < slot_count; ++i) {
        const auto length = readU32(data, offset);
        offset += 4;
        require(length, offset);
        std::string slot(length, '\0');
        std::memcpy(slot.data(), data.data() + offset, length);
        offset += length;
        material_slots.push_back(std::move(slot));
    }

    rendering::AABB bounds;
    for (const auto& vertex: vertices) {
        bounds.expand(vertex.position);
    }

    return std::make_shared<MeshAsset>(handle, std::move(vertices), std::move(indices),
            std::move(submeshes), std::move(material_slots), bounds);
}

}
