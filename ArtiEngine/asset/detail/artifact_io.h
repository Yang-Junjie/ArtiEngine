#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iosfwd>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace arti::engine::asset::detail {

inline void writeU32(std::ostream& output, uint32_t value) {
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

inline void writeU64(std::ostream& output, uint64_t value) {
    const std::array<char, 8> bytes{
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
        static_cast<char>((value >> 32) & 0xff),
        static_cast<char>((value >> 40) & 0xff),
        static_cast<char>((value >> 48) & 0xff),
        static_cast<char>((value >> 56) & 0xff),
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

inline void writeF32(std::ostream& output, float value) {
    writeU32(output, std::bit_cast<uint32_t>(value));
}

inline uint32_t readU32(const std::vector<std::byte>& data, size_t offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("The artifact header is truncated.");
    }
    return (static_cast<uint32_t>(std::to_integer<unsigned char>(data[offset]))) |
           (static_cast<uint32_t>(std::to_integer<unsigned char>(data[offset + 1])) << 8) |
           (static_cast<uint32_t>(std::to_integer<unsigned char>(data[offset + 2])) << 16) |
           (static_cast<uint32_t>(std::to_integer<unsigned char>(data[offset + 3])) << 24);
}

inline uint64_t readU64(const std::vector<std::byte>& data, size_t offset) {
    if (offset + 8 > data.size()) {
        throw std::runtime_error("The artifact header is truncated.");
    }
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) {
        value |= static_cast<uint64_t>(std::to_integer<unsigned char>(data[offset + index]))
                 << (8 * index);
    }
    return value;
}

inline float readF32(const std::vector<std::byte>& data, size_t offset) {
    return std::bit_cast<float>(readU32(data, offset));
}

inline std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open '" + path.string() + "' for reading.");
    }

    std::string contents{ std::istreambuf_iterator<char>{ input },
        std::istreambuf_iterator<char>{} };
    if (!input.good() && !input.eof()) {
        throw std::runtime_error("Failed while reading '" + path.string() + "'.");
    }
    return contents;
}

inline std::vector<std::byte> readFileBinary(const std::filesystem::path& path) {
    std::ifstream input{ path, std::ios::binary };
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open '" + path.string() + "' for reading.");
    }

    const std::string contents{ std::istreambuf_iterator<char>{ input },
        std::istreambuf_iterator<char>{} };
    if (!input.good() && !input.eof()) {
        throw std::runtime_error("Failed while reading '" + path.string() + "'.");
    }

    std::vector<std::byte> bytes(contents.size());
    for (size_t index = 0; index < contents.size(); ++index) {
        bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(contents[index]));
    }
    return bytes;
}

} // namespace arti::engine::asset::detail
