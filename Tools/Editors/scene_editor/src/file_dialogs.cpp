#include "file_dialogs.h"

#ifdef _WIN32

#define NOMINMAX
#include <shobjidl.h>
#include <windows.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace arti::editor::FileDialogs {
namespace {

struct DialogFilters {
    std::vector<std::wstring> names;
    std::vector<std::wstring> specs;
    std::vector<COMDLG_FILTERSPEC> filters;
};

class ScopedCom {
public:
    ScopedCom()
            : m_result(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))
    {}

    ~ScopedCom() {
        if (SUCCEEDED(m_result)) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool initialized() const { return SUCCEEDED(m_result); }

private:
    HRESULT m_result;
};

std::wstring toWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

std::wstring toWide(const std::filesystem::path& path) {
    return path.wstring();
}

DialogFilters buildFilters(const char* filter) {
    DialogFilters result;
    if (filter == nullptr || filter[0] == '\0') {
        return result;
    }

    const char* current = filter;
    while (*current != '\0') {
        std::string name = current;
        current += name.size() + 1;

        std::string spec = name;
        if (*current != '\0') {
            spec = current;
            current += spec.size() + 1;
        }

        result.names.push_back(toWide(name));
        result.specs.push_back(toWide(spec));
    }

    if (result.names.size() == 1 && result.specs[0] == result.names[0]) {
        result.names[0] = L"Files";
    }

    result.filters.reserve(result.names.size());
    for (size_t i = 0; i < result.names.size(); ++i) {
        result.filters.push_back({ result.names[i].c_str(), result.specs[i].c_str() });
    }

    return result;
}

std::filesystem::path pathFromDialog(IShellItem* item) {
    PWSTR filePath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &filePath))) {
        return {};
    }

    std::filesystem::path path{ filePath };
    CoTaskMemFree(filePath);
    return path;
}

void setDefaultFolder(IFileDialog* dialog, const std::string& defaultPath) {
    if (defaultPath.empty()) {
        return;
    }

    const auto path = std::filesystem::absolute(std::filesystem::path{ toWide(defaultPath) });
    std::error_code error;
    const auto folderPath = std::filesystem::is_directory(path, error) ? path : path.parent_path();
    if (folderPath.empty()) {
        return;
    }

    IShellItem* folder = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(folderPath.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
        dialog->SetFolder(folder);
        folder->Release();
    }
}

void setDefaultFileName(IFileSaveDialog* dialog, const std::string& defaultPath) {
    if (defaultPath.empty()) {
        return;
    }

    const auto fileName = std::filesystem::path{ toWide(defaultPath) }.filename();
    if (fileName.empty()) {
        return;
    }

    const auto wideFileName = toWide(fileName);
    dialog->SetFileName(wideFileName.c_str());
}

std::filesystem::path showFileDialog(
        const CLSID& dialogId, const char* filter, const std::string& defaultPath) {
    ScopedCom com;
    if (!com.initialized()) {
        return {};
    }

    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(dialogId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return {};
    }

    setDefaultFolder(dialog, defaultPath);

    auto filters = buildFilters(filter);
    if (!filters.filters.empty()) {
        dialog->SetFileTypes(static_cast<UINT>(filters.filters.size()), filters.filters.data());
    }

    if (dialogId == CLSID_FileSaveDialog) {
        if (auto* saveDialog = static_cast<IFileSaveDialog*>(dialog)) {
            setDefaultFileName(saveDialog, defaultPath);
        }
    }

    std::filesystem::path result;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            result = pathFromDialog(item);
            item->Release();
        }
    }

    dialog->Release();
    return result;
}

} // namespace

std::filesystem::path openFile(const char* filter, const std::string& defaultPath) {
    return showFileDialog(CLSID_FileOpenDialog, filter, defaultPath);
}

std::filesystem::path saveFile(const char* filter, const std::string& defaultPath) {
    return showFileDialog(CLSID_FileSaveDialog, filter, defaultPath);
}

std::filesystem::path selectDirectory(const std::string& defaultPath) {
    ScopedCom com;
    if (!com.initialized()) {
        return {};
    }

    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(
                CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return {};
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS);
    }

    setDefaultFolder(dialog, defaultPath);

    std::filesystem::path result;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            result = pathFromDialog(item);
            item->Release();
        }
    }

    dialog->Release();
    return result;
}

} // namespace arti::editor::FileDialogs

#else

namespace arti::editor::FileDialogs {

std::filesystem::path openFile(const char*, const std::string&) {
    return {};
}

std::filesystem::path saveFile(const char*, const std::string&) {
    return {};
}

std::filesystem::path selectDirectory(const std::string&) {
    return {};
}

} // namespace arti::editor::FileDialogs

#endif
