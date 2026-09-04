#pragma once
#include "artichoco/asset/asset.h"

#include <string>
#include <string_view>
#include <utility>

namespace arti::engine::asset {

inline constexpr std::string_view kScriptAssetType{ "artiengine.asset.script" };

// 一段 Lua 源文本。artifact 和源文件逐字节相同 —— 脚本没有「编译产物」，importer
// 只是把它登记进 catalog 并拷进 Library/，好让 pack 和运行时走同一条 loader 路径。
class ScriptAsset final : public arti::asset::Asset {
public:
    ScriptAsset(core::UUID handle, std::string source)
            : Asset(handle),
              m_source(std::move(source)) {}

    arti::asset::AssetType getType() const override { return std::string{ kScriptAssetType }; }

    const std::string& source() const noexcept { return m_source; }

private:
    std::string m_source;
};

} // namespace arti::engine::asset
