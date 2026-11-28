#include "ApInstall.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include <nlohmann/json.hpp>

#include "ApWorld.h"
#include "host/FileDialogs.h"

namespace {
namespace fs = std::filesystem;

constexpr const char* kRememberedFile = "archipelago-install.json";
constexpr const char* kWorldFile = "fleet.apworld";
constexpr const char* kPlayerFolderKey = "player_files_path:";
constexpr const char* kHostNotesFile = "READ ME - Fleet for Archipelago.txt";
constexpr const char* kHostNotes = "Unzip this into your Archipelago folder, the one with ArchipelagoLauncher, and "
                                   "let it replace fleet.apworld if it asks.\r\n"
                                   "custom_worlds/fleet.apworld teaches Archipelago the game Fleet.\r\n"
                                   "Players/ holds this player's YAML.\r\n"
                                   "Restart the Archipelago launcher, then generate as usual.\r\n";

std::vector<fs::path> DefaultFolders() {
    std::vector<fs::path> folders;
#if defined(_WIN32)
    const char* programData = std::getenv("PROGRAMDATA");
    folders.push_back(fs::path(programData != nullptr ? programData : "C:/ProgramData") / "Archipelago");
#else
    const char* home = std::getenv("HOME");
    if (home != nullptr) {
#if defined(__APPLE__)
        folders.push_back(fs::path(home) / "Library" / "Application Support" / "Archipelago");
#endif
        folders.push_back(fs::path(home) / "Archipelago");
    }
#endif
    return folders;
}

bool IsArchipelagoFolder(const fs::path& folder) {
    std::error_code error;
    return !folder.empty() &&
           (fs::is_directory(folder / "custom_worlds", error) || fs::is_regular_file(folder / "host.yaml", error));
}

fs::path ReadRememberedFolder(const fs::path& exportFolder) {
    std::ifstream input(exportFolder / kRememberedFile);
    const nlohmann::json remembered = nlohmann::json::parse(input, nullptr, false);
    if (!remembered.is_object() || !remembered.value("folder", nlohmann::json()).is_string()) {
        return {};
    }
    return fs::u8path(remembered.at("folder").get<std::string>());
}

void RememberFolder(const fs::path& exportFolder, const fs::path& folder) {
    const auto text = folder.generic_u8string();
    std::ofstream(exportFolder / kRememberedFile)
        << nlohmann::json{ { "folder", std::string(text.begin(), text.end()) } }.dump(2);
}

// Empty when Archipelago is nowhere to be found and the player did not choose its folder.
fs::path FindArchipelagoFolder(const fs::path& exportFolder) {
    const fs::path remembered = ReadRememberedFolder(exportFolder);
    if (IsArchipelagoFolder(remembered)) {
        return remembered;
    }
    for (const fs::path& folder : DefaultFolders()) {
        if (IsArchipelagoFolder(folder)) {
            return folder;
        }
    }
    const fs::path chosen = ChooseFolder("Fleet could not find Archipelago: choose its folder");
    if (!IsArchipelagoFolder(chosen)) {
        return {};
    }
    RememberFolder(exportFolder, chosen);
    return chosen;
}

std::string TrimYamlValue(std::string value) {
    const size_t start = value.find_first_not_of(" \t\"'");
    const size_t end = value.find_last_not_of(" \t\"'\r");
    return start == std::string::npos ? "" : value.substr(start, end - start + 1);
}

// host.yaml's generator section can move the player folder away from Players.
fs::path FindPlayerFolder(const fs::path& archipelago) {
    std::ifstream hostYaml(archipelago / "host.yaml");
    std::string line;
    while (std::getline(hostYaml, line)) {
        const size_t key = line.find(kPlayerFolderKey);
        const size_t text = line.find_first_not_of(" \t");
        if (key == std::string::npos || line[text] == '#') {
            continue;
        }
        const fs::path configured = fs::u8path(TrimYamlValue(line.substr(key + std::string(kPlayerFolderKey).size())));
        if (!configured.empty()) {
            return configured.is_absolute() ? configured : archipelago / configured;
        }
    }
    return archipelago / "Players";
}

bool CopyInto(const fs::path& file, const fs::path& folder, std::string& error) {
    std::error_code copyError;
    fs::create_directories(folder, copyError);
    fs::copy_file(file, folder / file.filename(), fs::copy_options::overwrite_existing, copyError);
    if (copyError) {
        error = "could not copy " + file.filename().string() + " into " + folder.string() + ": " + copyError.message();
        return false;
    }
    return true;
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
} // namespace

std::string InstallIntoArchipelago(const fs::path& exportFolder, const std::string& slot) {
    const std::string yaml = slot + ".yaml";
    const fs::path archipelago = FindArchipelagoFolder(exportFolder);
    if (archipelago.empty()) {
        return "Exported to " + exportFolder.string() + ". Archipelago was not found: put " + kWorldFile +
               " in its custom_worlds folder and " + yaml + " in its Players folder.";
    }
    const fs::path players = FindPlayerFolder(archipelago);
    std::string error;
    if (!CopyInto(exportFolder / kWorldFile, archipelago / "custom_worlds", error) ||
        !CopyInto(exportFolder / yaml, players, error)) {
        return "Fleet " + error + ". Close Archipelago and press Install again.";
    }
    return "Installed: " + std::string(kWorldFile) + " is in custom_worlds and " + yaml + " in " + players.string() +
           ". Open Archipelago and generate.";
}

HostPackage PackageForHost(const fs::path& exportFolder, const std::string& slot) {
    const std::string yaml = slot + ".yaml";
    const std::vector<ZipEntry> entries = {
        { std::string("custom_worlds/") + kWorldFile, ReadBytes(exportFolder / kWorldFile) },
        { "Players/" + yaml, ReadBytes(exportFolder / yaml) },
        { kHostNotesFile, kHostNotes },
    };
    if (entries[0].bytes.empty() || entries[1].bytes.empty()) {
        return { {}, "The export is incomplete: export again." };
    }
    const fs::path zip = exportFolder / "for-host" / (slot + "-archipelago.zip");
    std::error_code folderError;
    fs::create_directories(zip.parent_path(), folderError);
    std::string error;
    if (!WriteZip(zip, entries, error)) {
        return { {}, "Fleet " + error + "." };
    }
    return { zip, "Send " + zip.string() +
                      " to the host. They unzip it into their Archipelago folder, the one with ArchipelagoLauncher, "
                      "and generate." };
}
