#include "AssetSetup.h"
#include "FileDialogs.h"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;

// The games' AssetSetupExit values and flags; keep them in step with their ModApi/AssetSetup.
constexpr int kExitDone = 0;
constexpr int kExitRunAgain = 3;
constexpr const char* kSetupFlag = "--fleet-asset-setup";
constexpr const char* kResultFileName = "fleet-asset-setup.txt";
constexpr int kMaxRunsPerGame = 3;

struct RomKind {
    const char* title;
    const char* configKey;
    const char* flag;
    const char* versions;
    std::vector<uint32_t> headerChecksums;
};

// Only the versions both the game and the other game's asset loader read.
const RomKind kOotRom = {
    "Ocarina of Time", "ootRom", "--oot-rom", "N64 NTSC 1.0, 1.1 or 1.2", { 0xEC7011B7, 0xD43DA81F, 0x693BA2AE }
};
const RomKind kMmRom = { "Majora's Mask", "mmRom", "--mm-rom", "N64 US or GameCube US", { 0x5354631C, 0xB443EB08 } };

#if defined(_WIN32)
constexpr const char* kOotLauncher = "soh.exe";
constexpr const char* kMmLauncher = "2ship.exe";
#elif defined(__APPLE__)
constexpr const char* kOotLauncher = "soh-macos";
constexpr const char* kMmLauncher = "2s2h-macos";
#else
constexpr const char* kOotLauncher = "soh.elf";
constexpr const char* kMmLauncher = "2s2h.elf";
#endif

struct SetupGame {
    const char* title;
    fs::path folder;
    const char* launcher;
    std::vector<const char*> ownArchives;
    const RomKind* ownRom;
    const char* assetLoader;
    const char* crossArchive;
    const RomKind* otherRom;
};

struct SetupRoms {
    fs::path oot;
    fs::path mm;
};

bool HasFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

bool HasOwnArchive(const SetupGame& game) {
    return std::any_of(game.ownArchives.begin(), game.ownArchives.end(),
                       [&](const char* name) { return HasFile(game.folder / name); });
}

// A disabled loader ends in .o2r.disabled and builds nothing, so only an enabled one counts.
bool HasPackage(const fs::path& mods, const std::string& fileName) {
    std::error_code error;
    auto entry = fs::recursive_directory_iterator(mods, error);
    for (; !error && entry != fs::recursive_directory_iterator(); entry.increment(error)) {
        if (entry->path().filename() == fileName) {
            return true;
        }
    }
    return false;
}

bool NeedsCrossArchive(const SetupGame& game) {
    const fs::path archive = game.folder / "mods" / game.crossArchive;
    return HasPackage(game.folder / "mods", game.assetLoader) && !HasFile(archive) &&
           !HasFile(archive.string() + ".pending");
}

bool NeedsSetup(const SetupGame& game) {
    return !HasOwnArchive(game) || NeedsCrossArchive(game);
}

std::string DescribeMissing(const SetupGame& game) {
    std::string missing;
    if (!HasOwnArchive(game)) {
        missing = game.ownArchives.front();
    }
    if (NeedsCrossArchive(game)) {
        missing += (missing.empty() ? "mods/" : " and mods/") + std::string(game.crossArchive);
    }
    return missing;
}

uint32_t ReadBigEndian(const std::array<uint8_t, 0x14>& bytes, size_t at) {
    return uint32_t(bytes[at]) << 24 | uint32_t(bytes[at + 1]) << 16 | uint32_t(bytes[at + 2]) << 8 | bytes[at + 3];
}

// .v64 dumps swap every byte pair and .n64 dumps every word; the first word tells which.
std::optional<uint32_t> ReadHeaderChecksum(const fs::path& rom) {
    std::array<uint8_t, 0x14> header{};
    std::ifstream file(rom, std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(header.data()), header.size())) {
        return std::nullopt;
    }
    const uint32_t magic = ReadBigEndian(header, 0);
    if (magic == 0x37804012) {
        for (size_t at = 0; at < header.size(); at += 2) {
            std::swap(header[at], header[at + 1]);
        }
    } else if (magic == 0x40123780) {
        for (size_t at = 0; at < header.size(); at += 4) {
            std::swap(header[at], header[at + 3]);
            std::swap(header[at + 1], header[at + 2]);
        }
    }
    if (ReadBigEndian(header, 0) != 0x80371240) {
        return std::nullopt;
    }
    return ReadBigEndian(header, 0x10);
}

bool IsSupportedRom(const fs::path& rom, const RomKind& kind) {
    const std::optional<uint32_t> checksum = ReadHeaderChecksum(rom);
    return checksum.has_value() &&
           std::find(kind.headerChecksums.begin(), kind.headerChecksums.end(), *checksum) != kind.headerChecksums.end();
}

std::string ToUtf8(const fs::path& path) {
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

void ShowMessage(SDL_MessageBoxFlags kind, const std::string& message) {
    SDL_ShowSimpleMessageBox(kind, "Fleet setup", message.c_str(), nullptr);
}

Json ReadConfig(const fs::path& fleetFolder) {
    std::ifstream input(fleetFolder / "fleet.json");
    Json config = Json::parse(input, nullptr, false);
    return config.is_object() ? config : Json::object();
}

// Only the path is kept, so an update that needs the archives again does not ask for the ROM again.
void RememberRom(const fs::path& fleetFolder, const RomKind& kind, const fs::path& rom) {
    Json config = ReadConfig(fleetFolder);
    config[kind.configKey] = ToUtf8(rom);
    std::ofstream(fleetFolder / "fleet.json") << config.dump(2) << '\n';
}

#ifdef _WIN32
std::wstring Widen(const std::string& utf8) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(size > 0 ? size : 0, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    return wide;
}

// The games read argv in the ANSI code page; a short path is ASCII even when the long one is not.
std::string ToArgument(const fs::path& path) {
    std::array<wchar_t, 32768> shortPath{};
    const DWORD length = GetShortPathNameW(path.c_str(), shortPath.data(), static_cast<DWORD>(shortPath.size()));
    if (length == 0 || length >= shortPath.size()) {
        return ToUtf8(path);
    }
    return ToUtf8(fs::path(std::wstring(shortPath.data(), length)));
}
#else
std::string ToArgument(const fs::path& path) {
    return path.string();
}
#endif

// Empty when the player gave up on this ROM.
fs::path ObtainRom(const fs::path& fleetFolder, const RomKind& kind) {
    const Json config = ReadConfig(fleetFolder);
    const auto remembered = config.find(kind.configKey);
    if (remembered != config.end() && remembered->is_string()) {
        const fs::path rom = fs::u8path(remembered->get<std::string>());
        if (IsSupportedRom(rom, kind)) {
            return rom;
        }
    }
    for (;;) {
        const fs::path chosen =
            ChooseFile(std::string("Choose your ") + kind.title + " ROM", "Nintendo 64 ROM", "*.z64 *.n64 *.v64");
        if (chosen.empty()) {
            return {};
        }
        if (IsSupportedRom(chosen, kind)) {
            RememberRom(fleetFolder, kind, chosen);
            return chosen;
        }
        ShowMessage(SDL_MESSAGEBOX_WARNING, ToUtf8(chosen.filename()) + " is not a " + kind.title +
                                                " ROM Fleet reads.\n\nUse an original " + kind.versions +
                                                " dump. Other regions and releases, such as LodgeNet, do not work.");
    }
}

// SDL needs its events pumped while a game runs, and the title shows the player that Fleet is still waiting.
class ProgressWindow {
  public:
    explicit ProgressWindow(std::string label) : mLabel(std::move(label)), mStart(std::chrono::steady_clock::now()) {
        SDL_InitSubSystem(SDL_INIT_VIDEO);
        mWindow =
            SDL_CreateWindow(mLabel.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 480, 80, SDL_WINDOW_SHOWN);
    }
    ~ProgressWindow() {
        if (mWindow != nullptr) {
            SDL_DestroyWindow(mWindow);
        }
    }
    ProgressWindow(const ProgressWindow&) = delete;
    ProgressWindow& operator=(const ProgressWindow&) = delete;

    void Update() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
        if (mWindow == nullptr) {
            return;
        }
        const auto elapsed = std::chrono::steady_clock::now() - mStart;
        const long long seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        const std::string clock =
            std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60);
        SDL_SetWindowTitle(mWindow, (mLabel + ", " + clock).c_str());
    }

  private:
    std::string mLabel;
    std::chrono::steady_clock::time_point mStart;
    SDL_Window* mWindow = nullptr;
};

std::vector<std::string> SetupArguments(const SetupRoms& roms) {
    std::vector<std::string> arguments = { kSetupFlag };
    if (!roms.oot.empty()) {
        arguments.push_back(kOotRom.flag);
        arguments.push_back(ToArgument(roms.oot));
    }
    if (!roms.mm.empty()) {
        arguments.push_back(kMmRom.flag);
        arguments.push_back(ToArgument(roms.mm));
    }
    return arguments;
}

#ifdef _WIN32
// Windows paths cannot hold quotes, so quoting each argument is enough.
std::optional<int> RunSetupProcess(const fs::path& launcher, const std::vector<std::string>& arguments,
                                   const fs::path& folder, ProgressWindow& progress) {
    std::wstring commandLine = L"\"" + launcher.wstring() + L"\"";
    for (const std::string& argument : arguments) {
        commandLine += L" \"" + Widen(argument) + L"\"";
    }
    STARTUPINFOW startup{ sizeof(startup) };
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(launcher.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(),
                        &startup, &process)) {
        return std::nullopt;
    }
    CloseHandle(process.hThread);
    while (WaitForSingleObject(process.hProcess, 50) == WAIT_TIMEOUT) {
        progress.Update();
    }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}
#else
// The games read the folder they start in as their data folder, so the child moves there before it runs.
std::optional<int> RunSetupProcess(const fs::path& launcher, const std::vector<std::string>& arguments,
                                   const fs::path& folder, ProgressWindow& progress) {
    std::vector<std::string> owned = { launcher.string() };
    owned.insert(owned.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (std::string& argument : owned) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    const pid_t child = fork();
    if (child < 0) {
        return std::nullopt;
    }
    if (child == 0) {
        if (chdir(folder.c_str()) == 0) {
            execv(launcher.c_str(), argv.data());
        }
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, WNOHANG) == 0) {
        progress.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return WIFEXITED(status) ? std::optional<int>(WEXITSTATUS(status)) : std::nullopt;
}
#endif

std::string ReadResultReason(const SetupGame& game) {
    std::ifstream input(game.folder / kResultFileName);
    std::string reason;
    std::getline(input, reason);
    return reason.empty() ? "it stopped before saying why" : reason;
}

// Empty when the game built everything; otherwise why it did not.
std::string RunGameSetup(const SetupGame& game, const SetupRoms& roms) {
    const fs::path launcher = game.folder / game.launcher;
    if (!HasFile(launcher)) {
        return ToUtf8(launcher) + " is missing";
    }
    for (int run = 0; run < kMaxRunsPerGame; run++) {
        std::error_code error;
        fs::remove(game.folder / kResultFileName, error);
        ProgressWindow progress(std::string("Fleet: preparing ") + game.title);
        const std::optional<int> code = RunSetupProcess(launcher, SetupArguments(roms), game.folder, progress);
        if (!code.has_value()) {
            return "it could not start, or it crashed";
        }
        if (*code == kExitDone) {
            break;
        }
        if (*code != kExitRunAgain) {
            return ReadResultReason(game);
        }
    }
    return NeedsSetup(game) ? "it finished without building " + DescribeMissing(game) : "";
}

std::vector<SetupGame> FindPendingGames(const fs::path& ootFolder, const fs::path& mmFolder) {
    const SetupGame games[] = {
        { "Ship of Harkinian",
          ootFolder,
          kOotLauncher,
          { "oot.o2r", "oot-mq.o2r" },
          &kOotRom,
          "mm_assets.o2r",
          "mm.o2r",
          &kMmRom },
        { "2 Ship 2 Harkinian", mmFolder, kMmLauncher, { "mm.o2r" }, &kMmRom, "oot_assets.o2r", "oot.o2r", &kOotRom },
    };
    std::vector<SetupGame> pending;
    for (const SetupGame& game : games) {
        const bool isInstalled = !game.folder.empty() && HasFile(game.folder / game.launcher);
        if (isInstalled && NeedsSetup(game)) {
            pending.push_back(game);
        }
    }
    return pending;
}

bool NeedsRom(const std::vector<SetupGame>& games, const RomKind& kind) {
    return std::any_of(games.begin(), games.end(), [&](const SetupGame& game) {
        return (game.ownRom == &kind && !HasOwnArchive(game)) || (game.otherRom == &kind && NeedsCrossArchive(game));
    });
}

enum class SetupChoice { Continue, Skip, Quit };

SetupChoice AskToSetUp(const std::vector<SetupGame>& games) {
    std::string message = "Fleet builds the game files from your own ROMs. This happens once, and the games open "
                          "and close by themselves while it runs.\n";
    for (const SetupGame& game : games) {
        message += std::string("\n") + game.title + ": " + DescribeMissing(game);
    }
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, static_cast<int>(SetupChoice::Continue), "Continue" },
        { 0, static_cast<int>(SetupChoice::Skip), "Not now" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, static_cast<int>(SetupChoice::Quit), "Quit" },
    };
    const SDL_MessageBoxData dialog{
        SDL_MESSAGEBOX_INFORMATION,           nullptr, "Fleet setup", message.c_str(),
        static_cast<int>(std::size(buttons)), buttons, nullptr,
    };
    int choice = static_cast<int>(SetupChoice::Quit);
    if (SDL_ShowMessageBox(&dialog, &choice) != 0) {
        return SetupChoice::Skip;
    }
    return static_cast<SetupChoice>(choice);
}

// None when the player gave one up; Fleet then plays what is already there.
std::optional<SetupRoms> ObtainNeededRoms(const fs::path& fleetFolder, const std::vector<SetupGame>& games) {
    SetupRoms roms;
    for (const auto& [kind, rom] : { std::pair{ &kOotRom, &roms.oot }, std::pair{ &kMmRom, &roms.mm } }) {
        if (!NeedsRom(games, *kind)) {
            continue;
        }
        *rom = ObtainRom(fleetFolder, *kind);
        if (rom->empty()) {
            return std::nullopt;
        }
    }
    return roms;
}
} // namespace

bool EnsureGameAssets(const fs::path& fleetFolder, const fs::path& ootFolder, const fs::path& mmFolder) {
    const std::vector<SetupGame> games = FindPendingGames(ootFolder, mmFolder);
    if (games.empty()) {
        return true;
    }
    const SetupChoice choice = AskToSetUp(games);
    if (choice != SetupChoice::Continue) {
        return choice == SetupChoice::Skip;
    }
    const std::optional<SetupRoms> roms = ObtainNeededRoms(fleetFolder, games);
    if (!roms.has_value()) {
        ShowMessage(SDL_MESSAGEBOX_INFORMATION, "Fleet builds the game files the next time it opens.");
        return true;
    }
    std::string problems;
    for (const SetupGame& game : games) {
        const std::string problem = RunGameSetup(game, *roms);
        if (!problem.empty()) {
            problems += std::string("\n") + game.title + ": " + problem + ".\nIts log is in " +
                        ToUtf8(game.folder / "logs") + "\n";
        }
    }
    if (!problems.empty()) {
        ShowMessage(SDL_MESSAGEBOX_ERROR, "Some game files could not be built:\n" + problems);
    }
    return true;
}
