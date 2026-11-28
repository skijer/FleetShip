#pragma once

#include <filesystem>

// Builds every archive the games read out of the player's ROMs: each game's own, and the other game's assets that
// its asset loader mod needs. Each ROM is chosen once, then remembered. False only when the player chose to quit.
bool EnsureGameAssets(const std::filesystem::path& fleetFolder, const std::filesystem::path& ootFolder,
                      const std::filesystem::path& mmFolder);
