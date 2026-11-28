#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rando/ArchipelagoExport.h"

// One export lives in <archipelago folder>/exports/<export id>/.
inline constexpr const char* kArchipelagoExportsFolder = "exports";
inline constexpr const char* kArchipelagoPartsFile = "parts.json";
inline constexpr const char* kArchipelagoMapFile = "map.json";
inline constexpr const char* kArchipelagoNamesFile = "names.json";

// The session seed of an export, its free checks holding what the multiworld placed there.
nlohmann::json FleetSeedgen_ComposeArchipelagoSeed(const std::filesystem::path& exportFolder,
                                                   const std::vector<ComboRando::Archipelago::ScoutedItem>& scouted,
                                                   int ownSlot, const std::map<int64_t, std::string>& ownItemNames);
