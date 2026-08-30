#pragma once

#include <filesystem>
#include <string>

namespace arti::editor {
namespace FileDialogs {

// `filter` is a null-separated list of label/spec pairs terminated by an
// extra null, e.g. "Arti Scene\0*.artiscene\0". Empty on cancel/failure.
std::filesystem::path openFile(const char* filter, const std::string& defaultPath);
std::filesystem::path saveFile(const char* filter, const std::string& defaultPath);
std::filesystem::path selectDirectory(const std::string& defaultPath);

} // namespace FileDialogs
} // namespace arti::editor
