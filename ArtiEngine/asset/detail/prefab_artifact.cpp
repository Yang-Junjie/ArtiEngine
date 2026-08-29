#include "asset/detail/prefab_artifact.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <yaml-cpp/yaml.h>

namespace arti::engine::asset::detail {
namespace {

YAML::Node writeUuid(core::UUID id) {
    return YAML::Node(id.isValid() ? id.toString() : std::string{});
}

core::UUID readUuid(const YAML::Node& node) {
    if (!node || !node.IsScalar()) {
        return {};
    }
    const auto text = node.as<std::string>();
    if (text.empty()) {
        return {};
    }
    const auto parsed = core::UUID::fromString(text);
    return parsed ? *parsed : core::UUID{};
}

YAML::Node writeMatrix(const glm::mat4& matrix) {
    YAML::Node node(YAML::NodeType::Sequence);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            node.push_back(matrix[column][row]);
        }
    }
    node.SetStyle(YAML::EmitterStyle::Flow);
    return node;
}

glm::mat4 readMatrix(const YAML::Node& node) {
    if (!node || !node.IsSequence() || node.size() != 16) {
        return glm::mat4{ 1.0f };
    }
    glm::mat4 matrix{ 1.0f };
    size_t index = 0;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            matrix[column][row] = node[index++].as<float>();
        }
    }
    return matrix;
}

}

std::vector<std::byte> encodePrefabArtifact(const std::vector<PrefabNode>& nodes) {
    YAML::Node root;
    YAML::Node node_list(YAML::NodeType::Sequence);

    for (const PrefabNode& node: nodes) {
        YAML::Node entry;
        entry["Name"] = node.name;
        entry["LocalTransform"] = writeMatrix(node.local_transform);
        entry["Parent"] = node.parent == kNoParentNode ? -1 : static_cast<int64_t>(node.parent);
        entry["Mesh"] = writeUuid(node.mesh);

        YAML::Node materials(YAML::NodeType::Sequence);
        for (const core::UUID material: node.materials) {
            materials.push_back(writeUuid(material));
        }
        materials.SetStyle(YAML::EmitterStyle::Flow);
        entry["Materials"] = materials;

        node_list.push_back(entry);
    }

    root["Nodes"] = node_list;

    YAML::Emitter emitter;
    emitter << root;
    const std::string text = emitter.c_str();

    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

std::shared_ptr<PrefabAsset> decodePrefabArtifact(core::UUID handle,
        const std::vector<std::byte>& data) {
    std::string text(data.size(), '\0');
    std::memcpy(text.data(), data.data(), data.size());

    const YAML::Node root = YAML::Load(text);
    const YAML::Node node_list = root["Nodes"];
    if (!node_list || !node_list.IsSequence()) {
        throw std::runtime_error("The prefab artifact has no Nodes sequence.");
    }

    std::vector<PrefabNode> nodes;
    nodes.reserve(node_list.size());
    for (const auto& entry: node_list) {
        PrefabNode node;
        if (entry["Name"]) {
            node.name = entry["Name"].as<std::string>();
        }
        node.local_transform = readMatrix(entry["LocalTransform"]);
        if (const auto parent = entry["Parent"]; parent) {
            const auto value = parent.as<int64_t>();
            node.parent = value < 0 ? kNoParentNode : static_cast<uint32_t>(value);
        }
        node.mesh = readUuid(entry["Mesh"]);
        if (const auto materials = entry["Materials"]; materials && materials.IsSequence()) {
            node.materials.reserve(materials.size());
            for (const auto& material: materials) {
                node.materials.push_back(readUuid(material));
            }
        }
        nodes.push_back(std::move(node));
    }

    return std::make_shared<PrefabAsset>(handle, std::move(nodes));
}

}
