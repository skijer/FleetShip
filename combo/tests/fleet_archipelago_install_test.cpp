#include "archipelago/ApInstall.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <zip.h>

namespace fs = std::filesystem;

namespace {

void Require(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

void Put(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << value;
}

std::string Read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

std::string ReadZipEntry(const fs::path& zip, const char* name) {
    int code = 0;
    zip_t* archive = zip_open(zip.string().c_str(), ZIP_RDONLY, &code);
    Require(archive != nullptr, "the host package does not open as a zip");
    zip_stat_t stat{};
    std::string bytes;
    if (zip_stat(archive, name, 0, &stat) == 0) {
        bytes.resize(stat.size);
        zip_file_t* file = zip_fopen(archive, name, 0);
        Require(file != nullptr && zip_fread(file, bytes.data(), stat.size) == static_cast<zip_int64_t>(stat.size),
                std::string("could not read ") + name);
        zip_fclose(file);
    }
    zip_discard(archive);
    return bytes;
}

void TestHostPackage(const fs::path& root) {
    const fs::path exportFolder = root / "export";
    Put(exportFolder / "fleet.apworld", "world");
    Put(exportFolder / "Bench.yaml", "yaml");
    const HostPackage package = PackageForHost(exportFolder, "Bench");
    Require(!package.zip.empty() && fs::exists(package.zip), "no host package was written: " + package.message);
    Require(ReadZipEntry(package.zip, "custom_worlds/fleet.apworld") == "world", "the zip lacks custom_worlds");
    Require(ReadZipEntry(package.zip, "Players/Bench.yaml") == "yaml", "the zip lacks the player's YAML");
    Require(!ReadZipEntry(package.zip, "READ ME - Fleet for Archipelago.txt").empty(), "the zip lacks its notes");
    Require(PackageForHost(exportFolder, "Nobody").zip.empty(), "an export without its YAML was packaged");
}

void TestLocalInstall(const fs::path& root) {
    const fs::path exportFolder = root / "export";
    const fs::path archipelago = root / "Archipelago";
    fs::create_directories(archipelago / "custom_worlds");
    Put(archipelago / "host.yaml", "generator:\n"
                                   "  # general weights file, within the stated player_files_path location\n"
                                   "  player_files_path: \"MyPlayers\"\n");
    Put(exportFolder / "archipelago-install.json", "{\"folder\": \"" + archipelago.generic_string() + "\"}");
    const std::string message = InstallIntoArchipelago(exportFolder, "Bench");
    Require(Read(archipelago / "custom_worlds" / "fleet.apworld") == "world",
            "the world was not installed: " + message);
    Require(Read(archipelago / "MyPlayers" / "Bench.yaml") == "yaml", "host.yaml's player folder was ignored");
}

} // namespace

int main() {
    const fs::path root =
        fs::temp_directory_path() /
        ("fleet-archipelago-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        TestHostPackage(root);
        TestLocalInstall(root);
        fs::remove_all(root);
        std::cout << "archipelago install and host package passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
