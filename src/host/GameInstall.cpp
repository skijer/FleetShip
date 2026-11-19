#include "GameInstall.h"
#include "ReleaseKeys.h"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <future>
#include <stdexcept>
#include <vector>
#include <nlohmann/json.hpp>
#include <monocypher.h>
#include <monocypher-ed25519.h>
#include <zip.h>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <shobjidl.h>
#endif

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;

struct GameInstall {
    const char* name;
    const char* folder;
    const char* module;
    const char* archive;
    const char* data;
    const char* manifest;
    const uint8_t* key;
};

#if defined(_WIN32)
constexpr const char* OotModule = "soh.dll";
constexpr const char* MmModule = "2ship.dll";
#elif defined(__APPLE__)
constexpr const char* OotModule = "libsoh.dylib";
constexpr const char* MmModule = "lib2ship.dylib";
#else
constexpr const char* OotModule = "libsoh.so";
constexpr const char* MmModule = "lib2ship.so";
#endif

const GameInstall Games[] = {
    { "Ship", "Ship", OotModule, "soh.o2r", "oot.o2r",
      "https://github.com/skijer/Shipwright/releases/latest/download/unbound-release.json", OotReleaseKey },
    { "2Ship", "2Ship", MmModule, "2ship.o2r", "mm.o2r",
      "https://github.com/skijer/2ship2harkinian/releases/latest/download/unbound-release.json", MmReleaseKey }
};

bool HasGameFiles(const fs::path& folder, const GameInstall& game) {
    return fs::is_regular_file(folder / game.module) && fs::is_regular_file(folder / game.archive) &&
           fs::is_regular_file(folder / game.data);
}

void ShowProblem(const std::string& message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fleet setup", message.c_str(), nullptr);
}

#ifdef _WIN32
class HttpHandle {
  public:
    explicit HttpHandle(HINTERNET value) : mValue(value) {
        if (value == nullptr) {
            throw std::runtime_error("Could not open an HTTPS connection.");
        }
    }
    ~HttpHandle() {
        WinHttpCloseHandle(mValue);
    }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
    operator HINTERNET() const {
        return mValue;
    }

  private:
    HINTERNET mValue;
};

std::vector<uint8_t> Download(const std::string& url, size_t limit, std::atomic<size_t>& progress) {
    if (url.rfind("https://", 0) != 0) {
        throw std::runtime_error("The download must use HTTPS.");
    }
    const int size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url.data(), static_cast<int>(url.size()), nullptr, 0);
    if (size <= 0) {
        throw std::runtime_error("Invalid download URL.");
    }
    std::wstring wide(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, url.data(), static_cast<int>(url.size()), wide.data(), size);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        throw std::runtime_error("Invalid HTTPS URL.");
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                              std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    HttpHandle session(
        WinHttpOpen(L"Fleet", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    WinHttpSetTimeouts(session, 20000, 20000, 20000, 30000);
    HttpHandle connection(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
    HttpHandle request(WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        throw std::runtime_error("The HTTPS download failed.");
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        throw std::runtime_error("Download returned HTTP " + std::to_string(status));
    }
    std::vector<uint8_t> bytes;
    std::array<uint8_t, 65536> buffer{};
    for (;;) {
        DWORD count = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &count)) {
            throw std::runtime_error("The download was interrupted.");
        }
        if (count == 0) {
            return bytes;
        }
        if (bytes.size() + count > limit) {
            throw std::runtime_error("Download exceeds its expected size.");
        }
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + count);
        progress.store(bytes.size());
    }
}

fs::path ChooseFolder() {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileOpenDialog* dialog = nullptr;
    fs::path result;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
    return result;
}
#else
std::vector<uint8_t> Download(const std::string&, size_t, std::atomic<size_t>&) {
    throw std::runtime_error("Automatic installation is currently available on Windows. Set oot/mm in fleet.json.");
}
fs::path ChooseFolder() {
    ShowProblem("Set oot/mm in fleet.json to the existing installation folders, then restart Fleet.");
    return {};
}
#endif

std::vector<uint8_t> DecodeHex(const std::string& value, size_t count) {
    if (value.size() != count * 2 || value.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        throw std::runtime_error("Invalid signature or hash encoding.");
    }
    std::vector<uint8_t> bytes(count);
    for (size_t i = 0; i < count; ++i) {
        bytes[i] = static_cast<uint8_t>(std::stoul(value.substr(i * 2, 2), nullptr, 16));
    }
    return bytes;
}

Json VerifyManifest(const std::vector<uint8_t>& bytes, const GameInstall& game) {
    if (std::all_of(game.key, game.key + 32, [](uint8_t value) { return value == 0; })) {
        throw std::runtime_error(std::string(game.name) +
                                 " has no trusted release key in this Fleet build. Choose an existing installation.");
    }
    const Json envelope = Json::parse(bytes.begin(), bytes.end());
    const std::string payload = envelope.at("payload").get<std::string>();
    const auto signature = DecodeHex(envelope.at("signature").get<std::string>(), 64);
    const std::string message = "unbound-release-v1\n" + payload;
    if (payload.empty() ||
        crypto_ed25519_check(signature.data(), game.key, reinterpret_cast<const uint8_t*>(message.data()),
                             message.size()) != 0) {
        throw std::runtime_error("The release signature does not match the game's trusted key.");
    }
    const Json release = Json::parse(payload);
    if (release.at("format") != 1 || release.at("version").get<std::string>().empty()) {
        throw std::runtime_error("Unsupported release manifest.");
    }
    return release;
}

void ExtractArchive(const std::vector<uint8_t>& bytes, const fs::path& target, const GameInstall& game) {
    zip_error_t error;
    zip_error_init(&error);
    zip_source_t* source = zip_source_buffer_create(bytes.data(), bytes.size(), 0, &error);
    zip_t* archive = source == nullptr ? nullptr : zip_open_from_source(source, ZIP_RDONLY, &error);
    if (archive == nullptr) {
        if (source != nullptr) {
            zip_source_free(source);
        }
        zip_error_fini(&error);
        throw std::runtime_error("The release asset is not a valid ZIP archive.");
    }
    zip_error_fini(&error);
    const fs::path staging = target.string() + ".installing";
    try {
        if (fs::exists(staging)) {
            throw std::runtime_error("An earlier install staging folder exists: " + staging.string());
        }
        fs::create_directories(staging);
        const zip_int64_t count = zip_get_num_entries(archive, 0);
        if (count < 1 || count > 100000) {
            throw std::runtime_error("Invalid ZIP entry count.");
        }
        uint64_t total = 0;
        for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(count); ++i) {
            zip_stat_t stat{};
            if (zip_stat_index(archive, i, 0, &stat) != 0 || stat.name == nullptr) {
                throw std::runtime_error("Cannot read ZIP entry.");
            }
            const std::string name(stat.name);
            const fs::path relative = fs::u8path(name);
            if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
                name.find('\\') != std::string::npos || name.find(':') != std::string::npos) {
                throw std::runtime_error("Unsafe ZIP path.");
            }
            for (const auto& part : relative) {
                if (part == "..") {
                    throw std::runtime_error("ZIP path escapes the installation.");
                }
            }
            zip_uint8_t system = 0;
            zip_uint32_t attributes = 0;
            zip_file_get_external_attributes(archive, i, 0, &system, &attributes);
            if (system == ZIP_OPSYS_UNIX && ((attributes >> 16) & 0170000) == 0120000) {
                throw std::runtime_error("ZIP symlinks are not allowed.");
            }
            total += stat.size;
            if (total > 4ull * 1024 * 1024 * 1024) {
                throw std::runtime_error("Extracted release exceeds 4 GiB.");
            }
            const fs::path output = staging / relative;
            if (name.back() == '/') {
                fs::create_directories(output);
                continue;
            }
            fs::create_directories(output.parent_path());
            zip_file_t* entry = zip_fopen_index(archive, i, 0);
            if (entry == nullptr) {
                throw std::runtime_error("Cannot open ZIP entry.");
            }
            std::ofstream file(output, std::ios::binary);
            std::array<char, 65536> buffer;
            zip_int64_t read = 0;
            uint64_t written = 0;
            while ((read = zip_fread(entry, buffer.data(), buffer.size())) > 0) {
                file.write(buffer.data(), read);
                written += read;
            }
            const int closed = zip_fclose(entry);
            file.close();
            if (read < 0 || closed != 0 || written != stat.size || !file) {
                throw std::runtime_error("Could not extract a complete ZIP entry.");
            }
        }
        fs::path install;
        for (const auto& entry : fs::recursive_directory_iterator(staging)) {
            if (entry.is_regular_file() && entry.path().filename() == game.module) {
                if (!install.empty()) {
                    throw std::runtime_error("Release contains more than one game installation.");
                }
                install = entry.path().parent_path();
            }
        }
        if (install.empty() || !fs::is_regular_file(install / game.archive)) {
            throw std::runtime_error("Release is missing its game DLL or port archive.");
        }
        if (fs::exists(target)) {
            const fs::path backup = target.string() + ".before-install";
            if (fs::exists(backup) || fs::is_symlink(target)) {
                throw std::runtime_error("Existing backup or linked game folder: choose an existing install instead.");
            }
            fs::rename(target, backup);
        }
        fs::rename(install, target);
    } catch (...) {
        zip_discard(archive);
        throw;
    }
    zip_discard(archive);
}

void InstallGame(const fs::path& target, const GameInstall& game) {
    if (std::all_of(game.key, game.key + 32, [](uint8_t value) { return value == 0; })) {
        throw std::runtime_error(std::string(game.name) +
                                 " has no trusted release key in this Fleet build. Choose an existing installation.");
    }
    std::atomic<size_t> progress{ 0 };
    auto work = std::async(std::launch::async, [&] {
        const Json release = VerifyManifest(Download(game.manifest, 4 * 1024 * 1024, progress), game);
        const Json asset = release.at("assets").at("windows_x64");
        const uint64_t size = asset.at("size").get<uint64_t>();
        if (size == 0 || size > 1024ull * 1024 * 1024) {
            throw std::runtime_error("Invalid Windows asset size.");
        }
        progress.store(0);
        const auto bytes = Download(asset.at("url").get<std::string>(), size, progress);
        const auto expected = DecodeHex(asset.at("hash").get<std::string>(), 64);
        std::array<uint8_t, 64> hash{};
        crypto_blake2b(hash.data(), hash.size(), bytes.data(), bytes.size());
        if (bytes.size() != size || !std::equal(hash.begin(), hash.end(), expected.begin())) {
            throw std::runtime_error("The Windows asset's size or BLAKE2b hash does not match the signed manifest.");
        }
        ExtractArchive(bytes, target, game);
    });
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow("Fleet download", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 480, 80, 0);
    while (work.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
        if (window != nullptr) {
            const std::string title = std::string("Downloading ") + game.name + ": " +
                                      std::to_string(progress.load() / 1024) + " KiB received";
            SDL_SetWindowTitle(window, title.c_str());
        }
    }
    if (window != nullptr) {
        SDL_DestroyWindow(window);
    }
    work.get();
}

void SaveFolder(const fs::path& folder, const char* key, const fs::path& gameFolder) {
    std::ifstream input(folder / "fleet.json");
    Json config = Json::parse(input, nullptr, false);
    if (!config.is_object()) {
        config = Json::object();
    }
    config[key] = gameFolder.generic_string();
    std::ofstream output(folder / "fleet.json");
    output << config.dump(2) << '\n';
    if (!output) {
        throw std::runtime_error("Could not save fleet.json.");
    }
}
} // namespace

bool EnsureGameInstalls(const fs::path& folder, std::string& oot, std::string& mm) {
    std::string* directories[] = { &oot, &mm };
    for (size_t i = 0; i < std::size(Games); ++i) {
        const auto& game = Games[i];
        while (!HasGameFiles(fs::u8path(*directories[i]), game)) {
            const std::string message =
                std::string(game.name) + " is missing or incomplete at:\n" + *directories[i] + "\n\nIt needs " +
                game.module + ", " + game.archive + " and " + game.data +
                ".\nAfter installation, open the game once to extract your ROM, then press Re-check.";
            const SDL_MessageBoxButtonData buttons[] = { { 0, 1, "Download" },
                                                         { 0, 2, "Choose folder" },
                                                         { 0, 3, "Re-check" },
                                                         { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" } };
            const SDL_MessageBoxData dialog{
                SDL_MESSAGEBOX_INFORMATION,           nullptr, "Fleet setup", message.c_str(),
                static_cast<int>(std::size(buttons)), buttons, nullptr
            };
            int choice = 0;
            if (SDL_ShowMessageBox(&dialog, &choice) != 0 || choice <= 0) {
                return false;
            }
            try {
                switch (choice) {
                    case 1: {
                        const fs::path target = folder / game.folder;
                        InstallGame(target, game);
                        *directories[i] = target.string();
                        SaveFolder(folder, i == 0 ? "oot" : "mm", target);
                        break;
                    }
                    case 2: {
                        const fs::path chosen = ChooseFolder();
                        if (!chosen.empty()) {
                            *directories[i] = chosen.string();
                            SaveFolder(folder, i == 0 ? "oot" : "mm", chosen);
                        }
                        break;
                    }
                    case 3:
                        break;
                    default:
                        return false;
                }
            } catch (const std::exception& error) { ShowProblem(error.what()); }
        }
    }
    return true;
}
