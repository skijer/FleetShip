#pragma once

#include <filesystem>
#include <string>

// Both take the folder an export was written to; the messages are for the player.

// Copies fleet.apworld into this computer's Archipelago custom_worlds and the slot's YAML into its player folder.
// Asks for Archipelago's folder once when it is not where its installer puts it.
std::string InstallIntoArchipelago(const std::filesystem::path& exportFolder, const std::string& slot);

struct HostPackage {
    std::filesystem::path zip;
    std::string message;
};

// Zips both into Archipelago's own layout, so the host unzips it into their Archipelago folder and generates.
// The zip path is empty when packaging failed.
HostPackage PackageForHost(const std::filesystem::path& exportFolder, const std::string& slot);
