#pragma once

#include <filesystem>

// Exports, the apworld, YAMLs and Archipelago sessions all live in <Fleet install>/archipelago.
inline std::filesystem::path ArchipelagoFolder(const std::filesystem::path& install) {
    return install / "archipelago";
}
