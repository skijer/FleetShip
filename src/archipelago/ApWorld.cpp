#include "ApWorld.h"

#include <fstream>
#include <vector>

#include <zip.h>

#include "seedgen/ArchipelagoSeed.h"

namespace {

constexpr const char* kWorldSource = R"PY("""Ocarina of Time and Majora's Mask, alone or combined, played through Fleet."""
import json
import pkgutil
from dataclasses import dataclass
from typing import Any, Callable

from BaseClasses import CollectionState, Item, ItemClassification, Location, Region
from Options import OptionDict, OptionError, PerGameCommonOptions
from worlds.AutoWorld import World

_NAMES = json.loads(pkgutil.get_data(__name__, "names.json").decode("utf-8"))
_GAME = "Fleet"
_VICTORY = "Victory"
_CLASSES = {
    "progression": ItemClassification.progression,
    "useful": ItemClassification.useful,
    "trap": ItemClassification.trap,
    "filler": ItemClassification.filler,
}


class FleetSeed(OptionDict):
    """The seed Fleet exported for this player. Fleet writes it; any edit breaks the game."""
    display_name = "Fleet Seed"


@dataclass
class FleetOptions(PerGameCommonOptions):
    fleet_seed: FleetSeed


class FleetItem(Item):
    game = _GAME


class FleetLocation(Location):
    game = _GAME


def _rule(player: int, alternatives: list) -> Callable[[CollectionState], bool]:
    sets = [[(tuple(need["items"]), int(need["count"])) for need in alternative] for alternative in alternatives]

    def can_reach(state: CollectionState) -> bool:
        return any(all(sum(state.count(name, player) for name in names) >= count for names, count in needs)
                   for needs in sets)

    return can_reach


class FleetWorld(World):
    """Ocarina of Time and Majora's Mask, alone or combined, played through Fleet."""
    game = _GAME
    options_dataclass = FleetOptions
    options: FleetOptions
    topology_present = False
    item_name_to_id = _NAMES["items"]
    location_name_to_id = _NAMES["locations"]

    def generate_early(self) -> None:
        seed = self.options.fleet_seed.value
        if seed.get("format") != 1:
            raise OptionError(f"{self.player_name}'s YAML holds no Fleet seed; export one from Fleet.")
        self.seed = seed
        self.classes = {item["name"]: _CLASSES[item["classification"]] for item in seed["items"]}

    def create_regions(self) -> None:
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)
        for entry in self.seed["locations"]:
            location = FleetLocation(self.player, entry["name"], self.location_name_to_id[entry["name"]], menu)
            location.access_rule = _rule(self.player, entry["rule"])
            menu.locations.append(location)
        goal = FleetLocation(self.player, "Fleet Goal", None, menu)
        goal.access_rule = _rule(self.player, self.seed["goal"])
        goal.place_locked_item(FleetItem(_VICTORY, ItemClassification.progression, None, self.player))
        menu.locations.append(goal)
        self.multiworld.completion_condition[self.player] = lambda state: state.has(_VICTORY, self.player)

    def create_item(self, name: str) -> FleetItem:
        classification = self.classes.get(name, ItemClassification.filler)
        return FleetItem(name, classification, self.item_name_to_id[name], self.player)

    def create_items(self) -> None:
        for entry in self.seed["items"]:
            for _ in range(entry["count"]):
                self.multiworld.itempool.append(self.create_item(entry["name"]))

    def get_filler_item_name(self) -> str:
        return self.seed["filler"]

    def fill_slot_data(self) -> dict[str, Any]:
        return {"exportId": self.seed["exportId"], "mode": self.seed["mode"]}
)PY";

} // namespace

bool WriteZip(const std::filesystem::path& path, const std::vector<ZipEntry>& entries, std::string& error) {
    int code = 0;
    zip_t* archive = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &code);
    if (archive == nullptr) {
        error = "could not create " + path.string();
        return false;
    }
    for (const ZipEntry& entry : entries) {
        zip_source_t* source = zip_source_buffer(archive, entry.bytes.data(), entry.bytes.size(), 0);
        if (source == nullptr || zip_file_add(archive, entry.path.c_str(), source, ZIP_FL_OVERWRITE) < 0) {
            zip_source_free(source);
            zip_discard(archive);
            error = "could not add " + entry.path + " to " + path.string();
            return false;
        }
    }
    if (zip_close(archive) != 0) {
        zip_discard(archive);
        error = "could not finish " + path.string();
        return false;
    }
    return true;
}

namespace {

// One apworld serves every Fleet player of a multiworld, so it carries the names of every export in the folder.
nlohmann::json MergeExportedNames(const std::filesystem::path& folder, const nlohmann::json& names) {
    nlohmann::json merged = names;
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator(folder / kArchipelagoExportsFolder, ignored)) {
        std::ifstream file(entry.path() / kArchipelagoNamesFile, std::ios::binary);
        const nlohmann::json other = nlohmann::json::parse(file, nullptr, false);
        if (!other.is_object()) {
            continue;
        }
        for (const char* table : { "items", "locations" }) {
            merged[table].update(other.value(table, nlohmann::json::object()));
        }
    }
    return merged;
}

} // namespace

bool WriteArchipelagoFiles(const std::filesystem::path& folder, const nlohmann::json& exportNames,
                           const nlohmann::json& seedData, const std::string& playerName, std::string& error) {
    std::filesystem::create_directories(folder);
    const nlohmann::json names = MergeExportedNames(folder, exportNames);
    const nlohmann::json manifest = { { "game", "Fleet" },
                                      { "version", 7 },
                                      { "compatible_version", 7 },
                                      { "world_version", "1.0.0" },
                                      { "minimum_ap_version", "0.6.4" },
                                      { "authors", { "Fleet" } } };
    // The buffers must outlive zip_close, which is when libzip reads them.
    const std::vector<ZipEntry> entries = { { "fleet/__init__.py", kWorldSource },
                                            { "fleet/names.json", names.dump() },
                                            { "fleet/archipelago.json", manifest.dump(2) } };
    if (!WriteZip(folder / "fleet.apworld", entries, error)) {
        return false;
    }
    // JSON is valid YAML, so the seed goes in verbatim.
    std::ofstream yaml(folder / (playerName + ".yaml"), std::ios::binary | std::ios::trunc);
    yaml << "name: " << nlohmann::json(playerName).dump() << "\n"
         << "description: " << nlohmann::json("Fleet " + seedData.value("mode", std::string{}) + " seed").dump() << "\n"
         << "game: Fleet\n"
         << "Fleet:\n"
         << "  fleet_seed: " << seedData.dump() << "\n";
    if (!yaml) {
        error = "could not write the YAML in " + folder.string();
        return false;
    }
    return true;
}
