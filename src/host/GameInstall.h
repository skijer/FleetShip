#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

bool EnsureGameInstalls(const std::filesystem::path& folder, std::string& oot, std::string& mm);

struct UpdateFolders {
    std::filesystem::path fleet;
    std::filesystem::path oot;
    std::filesystem::path mm;
};

enum class UpdateKind { Fleet, Game, Mod };

// One file or installation the latest signed releases replace; the asset fields come from a verified document.
struct UpdateEntry {
    UpdateKind kind;
    std::string label;
    std::string installed;
    std::string latest;
    std::filesystem::path target;
    std::string url;
    uint64_t size = 0;
    std::string hash;
};

struct UpdateReport {
    std::string unavailableReason;
    std::vector<UpdateEntry> entries;
    std::vector<std::string> problems;
};

// Fleet and both games: they only work at matching versions, so they are installed together.
UpdateReport CheckCoreUpdates(const UpdateFolders& folders);
// The store mods the player installed, plus any a Fleet mod needs but is missing.
UpdateReport CheckModUpdates(const UpdateFolders& folders);
// Assumes no game of this process is attached yet: the games and their mods are replaced in place.
void ApplyUpdate(const UpdateEntry& entry, const UpdateFolders& folders, std::atomic<size_t>& progress);
void RestartFleet();
void RemoveReplacedFiles(const std::filesystem::path& fleetFolder);
