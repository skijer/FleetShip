#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct ZipEntry {
    std::string path;
    std::string bytes;
};

// libzip reads the entries' bytes only when the archive closes, so they must outlive this call.
bool WriteZip(const std::filesystem::path& path, const std::vector<ZipEntry>& entries, std::string& error);

// Writes fleet.apworld (the world and this install's name tables) and the player's YAML carrying the seed.
bool WriteArchipelagoFiles(const std::filesystem::path& folder, const nlohmann::json& exportNames,
                           const nlohmann::json& seedData, const std::string& playerName, std::string& error);
