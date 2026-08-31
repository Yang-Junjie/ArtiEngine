#pragma once
#include "artichoco/asset/asset_catalog.h"
#include "artichoco/asset/asset_importer.h"
#include "artichoco/asset/asset_storage.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace arti::engine::asset::detail {

class EngineImporter : public arti::asset::AssetImporter {
protected:
    std::filesystem::path resolveSourceFile(const std::filesystem::path& source_path) const {
        const auto file = m_storage->resolveSourcePath(source_path);
        if (!file) {
            throw std::runtime_error("Failed to resolve the source '" + source_path.string() +
                                     "'.");
        }
        return *file;
    }

    // 一个产出。identity 是 (source_path, local_id)：local_id 为空表示源文件
    // 本身就是唯一产出，非空表示子资产。
    //
    // local_id 优先用源文件里的稳定名字（glTF 的 mesh/material name、OBJ 的
    // shape/mtl 名），不要用下标 —— 下标是位置相关的，在源文件里插入一个 mesh
    // 会让同一个 UUID 指向另一块几何，场景引用静默错位。名字缺失时才回退下标。
    arti::asset::AssetImportOutput startOutput(const std::filesystem::path& source_path,
            std::string local_id, std::string_view type,
            std::string_view artifact_extension) const {
        const auto existing = m_catalog->findBySourceAndLocalId(source_path, local_id);

        arti::asset::AssetImportOutput output;
        output.record.local_id = std::move(local_id);
        output.record.handle = existing ? existing->handle : core::UUID::generate();
        output.record.type = std::string{ type };
        output.record.artifact_path = std::filesystem::path{ "Imported" } /
                                      (output.record.handle.toString() +
                                              std::string{ artifact_extension });
        return output;
    }
};

}
