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

    arti::asset::AssetImportOutput startOutput(const std::filesystem::path& source_path,
            std::string suffix, std::string_view type,
            std::string_view artifact_extension) const {
        std::filesystem::path identity = source_path;
        identity += suffix;

        const auto existing = m_catalog->findBySourcePathAndType(identity, std::string{ type });

        arti::asset::AssetImportOutput output;
        output.source_suffix = std::move(suffix);
        output.metadata.handle = existing ? existing->handle : core::UUID::generate();
        output.metadata.type = std::string{ type };
        output.metadata.artifact_path = std::filesystem::path{ "Imported" } /
                                       (output.metadata.handle.toString() +
                                               std::string{ artifact_extension });
        return output;
    }
};

}
