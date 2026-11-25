#include "GameInstall.h"
#include "ReleaseKeys.h"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <optional>
#include <set>
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

constexpr const char* ReleaseDomain = "unbound-release-v1";
constexpr const char* CatalogDomain = "unbound-catalog-v1";

bool IsUnsetKey(const uint8_t* key) {
    return std::all_of(key, key + 32, [](uint8_t value) { return value == 0; });
}

std::string OpenSignedDocument(const std::vector<uint8_t>& bytes, const uint8_t* key, const std::string& domain) {
    const Json envelope = Json::parse(bytes.begin(), bytes.end());
    const std::string payload = envelope.at("payload").get<std::string>();
    const auto signature = DecodeHex(envelope.at("signature").get<std::string>(), 64);
    const std::string message = domain + "\n" + payload;
    if (payload.empty() || crypto_ed25519_check(signature.data(), key, reinterpret_cast<const uint8_t*>(message.data()),
                                                message.size()) != 0) {
        throw std::runtime_error("The signature does not match the trusted key.");
    }
    return payload;
}

Json ReadReleaseManifest(const std::vector<uint8_t>& bytes, const uint8_t* key) {
    const Json release = Json::parse(OpenSignedDocument(bytes, key, ReleaseDomain));
    if (release.at("format") != 1 || release.at("version").get<std::string>().empty()) {
        throw std::runtime_error("Unsupported release manifest.");
    }
    return release;
}

Json VerifyManifest(const std::vector<uint8_t>& bytes, const GameInstall& game) {
    if (IsUnsetKey(game.key)) {
        throw std::runtime_error(std::string(game.name) +
                                 " has no trusted release key in this Fleet build. Choose an existing installation.");
    }
    return ReadReleaseManifest(bytes, game.key);
}

void RequireHash(const std::vector<uint8_t>& bytes, uint64_t size, const std::string& hash) {
    const auto expected = DecodeHex(hash, 64);
    std::array<uint8_t, 64> actual{};
    crypto_blake2b(actual.data(), actual.size(), bytes.data(), bytes.size());
    if (bytes.size() != size || !std::equal(actual.begin(), actual.end(), expected.begin())) {
        throw std::runtime_error("The download's size or BLAKE2b hash does not match the signed document.");
    }
}

std::vector<uint8_t> DownloadVerified(const std::string& url, uint64_t size, const std::string& hash,
                                      std::atomic<size_t>& progress) {
    if (size == 0 || size > 1024ull * 1024 * 1024) {
        throw std::runtime_error("Invalid download size.");
    }
    progress.store(0);
    auto bytes = Download(url, size, progress);
    RequireHash(bytes, size, hash);
    return bytes;
}

// Every entry lands inside staging, which must not exist yet.
void ExtractZip(const std::vector<uint8_t>& bytes, const fs::path& staging) {
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
    } catch (...) {
        zip_discard(archive);
        throw;
    }
    zip_discard(archive);
}

fs::path FindFolderHolding(const fs::path& root, const std::string& fileName) {
    fs::path found;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().filename() == fileName) {
            if (!found.empty()) {
                throw std::runtime_error("The release holds more than one " + fileName + ".");
            }
            found = entry.path().parent_path();
        }
    }
    return found;
}

void ExtractArchive(const std::vector<uint8_t>& bytes, const fs::path& target, const GameInstall& game) {
    const fs::path staging = target.string() + ".installing";
    ExtractZip(bytes, staging);
    const fs::path install = FindFolderHolding(staging, game.module);
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
}

void InstallGame(const fs::path& target, const GameInstall& game) {
    if (IsUnsetKey(game.key)) {
        throw std::runtime_error(std::string(game.name) +
                                 " has no trusted release key in this Fleet build. Choose an existing installation.");
    }
    std::atomic<size_t> progress{ 0 };
    auto work = std::async(std::launch::async, [&] {
        const Json release = VerifyManifest(Download(game.manifest, 4 * 1024 * 1024, progress), game);
        const Json asset = release.at("assets").at("windows_x64");
        const auto bytes = DownloadVerified(asset.at("url").get<std::string>(), asset.at("size").get<uint64_t>(),
                                            asset.at("hash").get<std::string>(), progress);
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

namespace {
constexpr const char* FleetManifestUrl = "https://github.com/skijer/FleetShip/releases/latest/download/fleet-release.json";
constexpr const char* FleetInstallFile = "fleet-install.json";
constexpr const char* FleetStaging = "fleet.updating";
constexpr const char* ReplacedSuffix = ".old";
constexpr size_t DocumentLimit = 16 * 1024 * 1024;

struct ModCatalog {
    const char* gameKey;
    const char* shortName;
    const char* url;
    const uint8_t* key;
};

const ModCatalog Catalogs[] = {
    { "oot", "OoT", "https://github.com/skijer/Shipwright/releases/download/mods/catalog.json", OotModKey },
    { "mm", "MM", "https://github.com/skijer/2ship2harkinian/releases/download/mods/catalog.json", MmModKey },
};

const char* GameTitle(size_t game) {
    return game == 0 ? "Ocarina of Time" : "Majora's Mask";
}

std::vector<uint8_t> ReadFileBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

std::string HashHex(const std::vector<uint8_t>& bytes) {
    std::array<uint8_t, 64> hash{};
    crypto_blake2b(hash.data(), hash.size(), bytes.data(), bytes.size());
    std::string hex;
    for (const uint8_t byte : hash) {
        hex += "0123456789abcdef"[byte >> 4];
        hex += "0123456789abcdef"[byte & 15];
    }
    return hex;
}

std::vector<uint8_t> DownloadDocument(const std::string& url) {
    std::atomic<size_t> progress{ 0 };
    return Download(url, DocumentLimit, progress);
}

// The fork version string is compiled into the game module, so it is read without loading the game.
std::string ReadModuleVersion(const fs::path& module) {
    const auto bytes = ReadFileBytes(module);
    const std::string marker = "-unbound";
    const auto isVersionChar = [](uint8_t value) { return std::isdigit(value) || value == '.'; };
    for (auto found = std::search(bytes.begin(), bytes.end(), marker.begin(), marker.end()); found != bytes.end();
         found = std::search(found + 1, bytes.end(), marker.begin(), marker.end())) {
        auto begin = found;
        while (begin != bytes.begin() && isVersionChar(*(begin - 1))) {
            --begin;
        }
        auto end = found + marker.size();
        while (end != bytes.end() && isVersionChar(*end)) {
            ++end;
        }
        const bool isVersion = begin != found && end != found + marker.size();
        if (isVersion) {
            return std::string(begin, end);
        }
    }
    return "";
}

std::optional<Json> ReadPackageManifest(const fs::path& package) {
    int error = 0;
    zip_t* archive = zip_open(package.string().c_str(), ZIP_RDONLY, &error);
    if (archive == nullptr) {
        return std::nullopt;
    }
    std::optional<Json> manifest;
    zip_stat_t stat{};
    if (zip_stat(archive, "manifest.json", 0, &stat) == 0 && stat.size < DocumentLimit) {
        std::string text(stat.size, '\0');
        zip_file_t* file = zip_fopen(archive, "manifest.json", 0);
        if (file != nullptr && zip_fread(file, text.data(), stat.size) == static_cast<zip_int64_t>(stat.size)) {
            manifest = Json::parse(text, nullptr, false);
        }
        if (file != nullptr) {
            zip_fclose(file);
        }
    }
    zip_discard(archive);
    return manifest;
}

std::string VersionText(const Json& value) {
    return value.is_string() ? value.get<std::string>() : value.is_number() ? value.dump() : "";
}

std::optional<Json> ReadInstallInfo(const fs::path& fleetFolder) {
    std::ifstream input(fleetFolder / FleetInstallFile);
    if (!input) {
        return std::nullopt;
    }
    Json info = Json::parse(input, nullptr, false);
    return info.is_object() ? std::optional<Json>(info) : std::nullopt;
}

UpdateEntry EntryFromAsset(UpdateKind kind, const Json& asset) {
    UpdateEntry entry{ kind };
    entry.url = asset.at("url").get<std::string>();
    entry.size = asset.at("size").get<uint64_t>();
    entry.hash = asset.at("hash").get<std::string>();
    return entry;
}

void AddFleetEntry(UpdateReport& report, const UpdateFolders& folders, const std::string& installed) {
    const Json release = ReadReleaseManifest(DownloadDocument(FleetManifestUrl), FleetReleaseKey);
    const std::string latest = release.at("version").get<std::string>();
    if (latest == installed) {
        return;
    }
    UpdateEntry entry = EntryFromAsset(UpdateKind::Fleet, release.at("assets").at("windows_x64"));
    entry.label = "Fleet";
    entry.installed = installed;
    entry.latest = latest;
    entry.target = folders.fleet;
    report.entries.push_back(std::move(entry));
}

void AddGameEntry(UpdateReport& report, size_t game, const fs::path& folder) {
    const Json release = VerifyManifest(DownloadDocument(Games[game].manifest), Games[game]);
    const std::string latest = release.at("version").get<std::string>();
    const std::string installed = ReadModuleVersion(folder / Games[game].module);
    if (installed == latest) {
        return;
    }
    UpdateEntry entry = EntryFromAsset(UpdateKind::Game, release.at("assets").at("windows_x64"));
    entry.label = GameTitle(game);
    entry.installed = installed.empty() ? "unknown" : installed;
    entry.latest = latest;
    entry.target = folder;
    report.entries.push_back(std::move(entry));
}

const Json* FindModBuild(const Json& mod, const char* gameKey) {
    const auto builds = mod.find("builds");
    if (builds != mod.end() && builds->is_object() && builds->contains(gameKey)) {
        return &builds->at(gameKey);
    }
    const auto build = mod.find("build");
    return build != mod.end() && build->is_object() ? &*build : nullptr;
}

std::vector<fs::path> ListPackages(const fs::path& folder) {
    std::vector<fs::path> packages;
    std::error_code error;
    for (const auto& file : fs::directory_iterator(folder, error)) {
        const std::string name = file.path().filename().string();
        if (name.ends_with(".o2r") || name.ends_with(".o2r.disabled")) {
            packages.push_back(file.path());
        }
    }
    return packages;
}

std::string PackageId(const fs::path& package) {
    const std::string name = package.filename().string();
    return name.substr(0, name.find(".o2r"));
}

// Fleet's own mods only work with what the store lists as their dependencies, transitively.
std::set<std::string> FindFleetDependencies(const Json& mods, const fs::path& fleetMods) {
    std::set<std::string> needed;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        if (!mods.contains(id)) {
            return;
        }
        for (const Json& dependency : mods.at(id).value("dependencies", Json::array())) {
            if (dependency.is_string() && needed.insert(dependency.get<std::string>()).second) {
                visit(dependency.get<std::string>());
            }
        }
    };
    for (const fs::path& package : ListPackages(fleetMods)) {
        visit(PackageId(package));
    }
    return needed;
}

UpdateEntry ModEntry(const ModCatalog& catalog, const Json& mod, const std::string& id, const Json& build) {
    UpdateEntry entry = EntryFromAsset(UpdateKind::Mod, build);
    entry.label = mod.value("name", id) + " mod (" + catalog.shortName + ")";
    entry.latest = VersionText(mod.value("version", Json()));
    return entry;
}

void AddModEntries(UpdateReport& report, const ModCatalog& catalog, const fs::path& gameFolder,
                   const fs::path& fleetFolder) {
    const Json payload = Json::parse(OpenSignedDocument(DownloadDocument(catalog.url), catalog.key, CatalogDomain));
    const Json& mods = payload.at("mods");
    const fs::path storeFolder = gameFolder / "mods" / "neistore";
    const fs::path fleetMods = fleetFolder / "mods" / catalog.gameKey;
    std::set<std::string> installed;
    for (const fs::path& folder : { gameFolder / "mods", storeFolder, fleetMods }) {
        for (const fs::path& package : ListPackages(folder)) {
            const std::string id = PackageId(package);
            const bool isNew = installed.insert(id).second;
            const Json* build = mods.contains(id) ? FindModBuild(mods.at(id), catalog.gameKey) : nullptr;
            if (!isNew || package.extension() != ".o2r" || build == nullptr ||
                build->value("hash", "") == HashHex(ReadFileBytes(package))) {
                continue;
            }
            UpdateEntry entry = ModEntry(catalog, mods.at(id), id, *build);
            const auto manifest = ReadPackageManifest(package);
            entry.installed = manifest ? VersionText(manifest->value("version", Json())) : "";
            entry.target = package;
            report.entries.push_back(std::move(entry));
        }
    }
    for (const std::string& id : FindFleetDependencies(mods, fleetMods)) {
        const Json* build = mods.contains(id) ? FindModBuild(mods.at(id), catalog.gameKey) : nullptr;
        if (installed.contains(id) || build == nullptr) {
            continue;
        }
        UpdateEntry entry = ModEntry(catalog, mods.at(id), id, *build);
        entry.installed = "missing";
        entry.target = storeFolder / (id + ".o2r");
        report.entries.push_back(std::move(entry));
    }
}

// Both games need the other game's assets extracted from the player's own ROM; nothing can download them.
void AddMissingExtractionNotes(UpdateReport& report, const fs::path& ootFolder, const fs::path& mmFolder) {
    if (!fs::exists(ootFolder / "mods" / "mm.o2r")) {
        report.problems.push_back("Ocarina of Time has no Majora's Mask assets: open Ship\\soh.exe once and choose "
                                  "your Majora's Mask ROM when it asks.");
    }
    if (!fs::exists(mmFolder / "mods" / "oot.o2r")) {
        report.problems.push_back("Majora's Mask has no Ocarina of Time assets: open 2Ship\\2ship.exe once and "
                                  "choose your Ocarina of Time ROM when it asks.");
    }
}

void CopyTree(const fs::path& source, const fs::path& target) {
    for (const auto& file : fs::recursive_directory_iterator(source)) {
        if (!file.is_regular_file()) {
            continue;
        }
        const fs::path destination = target / fs::relative(file.path(), source);
        fs::create_directories(destination.parent_path());
        fs::copy_file(file.path(), destination, fs::copy_options::overwrite_existing);
    }
}

// A running executable or a loaded library cannot be overwritten, but it can be renamed out of the way.
void ReplaceInUseFile(const fs::path& source, const fs::path& target) {
    if (fs::exists(target)) {
        const fs::path replaced = target.string() + ReplacedSuffix;
        std::error_code error;
        fs::remove(replaced, error);
        fs::rename(target, replaced);
    }
    fs::copy_file(source, target);
}

void ApplyMod(const UpdateEntry& entry, std::atomic<size_t>& progress) {
    const auto bytes = DownloadVerified(entry.url, entry.size, entry.hash, progress);
    const fs::path download = entry.target.string() + ".download";
    fs::create_directories(entry.target.parent_path());
    std::ofstream(download, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    fs::rename(download, entry.target);
}

void ApplyGame(const UpdateEntry& entry, const UpdateFolders& folders, std::atomic<size_t>& progress) {
    const auto bytes = DownloadVerified(entry.url, entry.size, entry.hash, progress);
    const GameInstall& game = entry.target == folders.oot ? Games[0] : Games[1];
    const fs::path staging = entry.target.string() + ".updating";
    fs::remove_all(staging);
    ExtractZip(bytes, staging);
    const fs::path package = FindFolderHolding(staging, game.module);
    if (package.empty()) {
        throw std::runtime_error(std::string(game.name) + " release is missing its game files.");
    }
    CopyTree(package, entry.target);
    fs::remove_all(staging);
}

// Only Fleet's own files: the games beside it are updated as their own entries.
void ApplyFleet(const UpdateEntry& entry, std::atomic<size_t>& progress) {
    const auto bytes = DownloadVerified(entry.url, entry.size, entry.hash, progress);
    const fs::path staging = entry.target / FleetStaging;
    fs::remove_all(staging);
    ExtractZip(bytes, staging);
    const fs::path package = FindFolderHolding(staging, FleetInstallFile);
    if (package.empty()) {
        throw std::runtime_error("The Fleet release is missing its install description.");
    }
    for (const auto& file : fs::directory_iterator(package)) {
        if (file.is_regular_file()) {
            ReplaceInUseFile(file.path(), entry.target / file.path().filename());
        } else if (file.path().filename() == "mods") {
            CopyTree(file.path(), entry.target / "mods");
        }
    }
    fs::remove_all(staging);
}
} // namespace

UpdateReport CheckForUpdates(const UpdateFolders& folders) {
    UpdateReport report;
#ifndef _WIN32
    report.unavailableReason = "Fleet downloads updates on Windows; here, download the latest Fleet package.";
    return report;
#endif
    const auto info = ReadInstallInfo(folders.fleet);
    if (!info) {
        report.unavailableReason = "This Fleet was not installed from a release package, so it does not update.";
        return report;
    }
    if (info->value("edition", "") != "release") {
        report.unavailableReason = "The modders edition does not update itself; download its latest package instead.";
        return report;
    }
    const auto attempt = [&report](const std::string& source, const auto& check) {
        try {
            check();
        } catch (const std::exception& error) { report.problems.push_back(source + ": " + error.what()); }
    };
    attempt("Fleet", [&] { AddFleetEntry(report, folders, info->value("version", "")); });
    const fs::path gameFolders[] = { folders.oot, folders.mm };
    for (size_t game = 0; game < std::size(Games); ++game) {
        attempt(GameTitle(game), [&] { AddGameEntry(report, game, gameFolders[game]); });
    }
    for (size_t game = 0; game < std::size(Catalogs); ++game) {
        attempt(std::string(GameTitle(game)) + " mods",
                [&] { AddModEntries(report, Catalogs[game], gameFolders[game], folders.fleet); });
    }
    AddMissingExtractionNotes(report, folders.oot, folders.mm);
    return report;
}

void ApplyUpdate(const UpdateEntry& entry, const UpdateFolders& folders, std::atomic<size_t>& progress) {
    switch (entry.kind) {
        case UpdateKind::Mod:
            ApplyMod(entry, progress);
            break;
        case UpdateKind::Game:
            ApplyGame(entry, folders, progress);
            break;
        case UpdateKind::Fleet:
            ApplyFleet(entry, progress);
            break;
        default:
            throw std::runtime_error("Unknown update kind.");
    }
}

void RestartFleet() {
#ifdef _WIN32
    std::array<wchar_t, 32768> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        throw std::runtime_error("Could not find Fleet's executable to restart it.");
    }
    std::wstring commandLine = L"\"" + std::wstring(path.data(), length) + L"\"";
    const std::wstring folder = fs::path(path.data()).parent_path().wstring();
    STARTUPINFOW startup{ sizeof(startup) };
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(), &startup,
                        &process)) {
        throw std::runtime_error("Could not restart Fleet.");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#endif
}

void RemoveReplacedFiles(const fs::path& fleetFolder) {
    if (!ReadInstallInfo(fleetFolder)) {
        return;
    }
    std::error_code error;
    fs::remove_all(fleetFolder / FleetStaging, error);
    for (const auto& file : fs::directory_iterator(fleetFolder, error)) {
        if (file.path().extension() == ReplacedSuffix) {
            fs::remove(file.path(), error);
        }
    }
}
