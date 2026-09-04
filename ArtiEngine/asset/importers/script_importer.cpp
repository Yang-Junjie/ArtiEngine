#include "asset/importers/script_importer.h"

#include "asset/detail/artifact_io.h"
#include "asset/script_asset.h"

#include <stdexcept>
#include <utility>

namespace arti::engine::asset {

std::vector<std::string> ScriptImporter::getSupportedExtensions() const {
    return { ".lua" };
}

arti::asset::AssetImportResult ScriptImporter::import(
        const arti::asset::AssetImportRequest& request) {
    arti::asset::AssetImportResult result;
    try {
        const auto file = resolveSourceFile(request.source_path);
        const std::string text = detail::readTextFile(file);

        // local_id 为空：一个 .lua 就是一个脚本资产。
        auto output = startOutput(request.source_path, {}, kScriptAssetType, ".artiscript");
        output.record.properties["byte_size"] = static_cast<uint64_t>(text.size());

        output.encoded.resize(text.size());
        for (size_t index = 0; index < text.size(); ++index) {
            output.encoded[index] = static_cast<std::byte>(static_cast<unsigned char>(text[index]));
        }
        result.outputs.push_back(std::move(output));
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.outputs.clear();
    }
    return result;
}

} // namespace arti::engine::asset
