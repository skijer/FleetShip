#include "FileDialogs.h"

#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include "portable-file-dialogs.h"
#endif

namespace {
namespace fs = std::filesystem;

#ifdef _WIN32
std::wstring Widen(const std::string& utf8) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(size > 0 ? size : 0, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    return wide;
}

// The Windows filter syntax separates patterns with ';'.
std::wstring WindowsPatterns(const std::string& patterns) {
    std::wstring joined = Widen(patterns);
    for (wchar_t& character : joined) {
        if (character == L' ') {
            character = L';';
        }
    }
    return joined;
}

fs::path ShowDialog(const std::string& title, bool isFolder, const std::string& filterName,
                    const std::string& patterns) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileOpenDialog* dialog = nullptr;
    fs::path result;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | (isFolder ? FOS_PICKFOLDERS : 0));
        dialog->SetTitle(Widen(title).c_str());
        const std::wstring name = Widen(filterName);
        const std::wstring spec = WindowsPatterns(patterns);
        const COMDLG_FILTERSPEC filters[] = { { name.c_str(), spec.c_str() }, { L"All files", L"*.*" } };
        if (!isFolder) {
            dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
        }
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->Show(nullptr)) && SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
        dialog->Release();
    }
    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
    return result;
}
#endif
} // namespace

fs::path ChooseFile(const std::string& title, const std::string& filterName, const std::string& patterns) {
#ifdef _WIN32
    return ShowDialog(title, false, filterName, patterns);
#else
    const std::vector<std::string> chosen =
        pfd::open_file(title, ".", { filterName, patterns, "All files", "*" }).result();
    return chosen.empty() ? fs::path() : fs::u8path(chosen.front());
#endif
}

fs::path ChooseFolder(const std::string& title) {
#ifdef _WIN32
    return ShowDialog(title, true, "", "");
#else
    const std::string chosen = pfd::select_folder(title, ".").result();
    return chosen.empty() ? fs::path() : fs::u8path(chosen);
#endif
}

void RevealInFileManager(const fs::path& file) {
#if defined(_WIN32)
    const std::wstring arguments = L"/select,\"" + file.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
#else
    // The middle child exits at once and is reaped here, so the opener never lingers as a zombie of Fleet.
    const pid_t child = fork();
    if (child != 0) {
        if (child > 0) {
            waitpid(child, nullptr, 0);
        }
        return;
    }
    if (fork() != 0) {
        _exit(0);
    }
#if defined(__APPLE__)
    execlp("open", "open", "-R", file.c_str(), static_cast<char*>(nullptr));
#else
    execlp("xdg-open", "xdg-open", file.parent_path().c_str(), static_cast<char*>(nullptr));
#endif
    _exit(127);
#endif
}
