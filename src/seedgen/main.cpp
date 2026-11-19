
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <ship/Context.h>
#include <ship/GameRegistry.h>

#include "GameModule.h"
#include "ServeProtocol.h"
#include "rando/ComboPlaythrough.h"
#include "rando/CrossDungeonCoordinator.h"
#include "rando/CrossDungeonGeneration.h"
#include "rando/CrossForeign.h"
#include "rando/CrossHints.h"
#include "rando/CrossWorldRando.h"
#include "rando/MmEntryGate.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

enum GameIndex { Oot, Mm, GameCount };

std::array<std::unique_ptr<GameModule>, GameCount> sGames;

enum class StartingGame { Oot, Mm, Random };

struct Options {
    std::string ootDirectory;
    std::string mmDirectory;
    uint32_t seed = 1;
    std::string outputPath;
    std::string settingsPath;
    ComboRando::CwGoal goal;
    StartingGame start = StartingGame::Oot;
    uint64_t sharedMask = 0;
    std::vector<std::string> unsharedPairs;
    bool plentifulModItems = false;
    std::string dumpDirectory;
    std::optional<ComboRando::CrossDungeon::PoolOptions> crossPools;
    nlohmann::json passages = nlohmann::json::array();
    std::string playthroughPath;
    std::string cataloguePath;
    std::string poolPath;
    std::string progressPath;
    std::string serveFolder;
    bool poolOnly = false;
    ComboRando::CwFillRules rules;
    std::string songs = "native";
    nlohmann::json fleetBlock;
    bool testChain = false;
};

struct Fill {
    ComboRando::CombinedFillResult result;
    uint32_t masterSeed;
    bool mmStart;
    std::string ootDump;
    std::string mmDump;
    std::string ootHintDump;
    ComboRando::RequirednessResult requiredness;
    nlohmann::json crossDungeonSeed;
    ComboRando::OracleFns ootOracle;
    ComboRando::OracleFns mmOracle;
};

const char* const kGameKeys[GameCount] = { "oot", "mm" };

using ItemTextMap = std::map<std::pair<std::string, std::string>, std::string>;

ItemTextMap ReadItemTexts(const std::string& ootDump, const std::string& mmDump, const char* field) {
    const std::string* dumps[GameCount] = { &ootDump, &mmDump };
    ItemTextMap texts;
    for (int game = 0; game < GameCount; game++) {
        const auto dump = nlohmann::json::parse(*dumps[game], nullptr, false);
        if (!dump.is_object()) {
            continue;
        }
        for (const auto& item : dump.value("items", nlohmann::json::array())) {
            if (item.contains(field)) {
                texts[{ kGameKeys[game], item.value("name", std::string{}) }] = item.value(field, std::string{});
            }
        }
    }
    return texts;
}

ItemTextMap ItemLabels(const std::string& ootDump, const std::string& mmDump) {
    return ReadItemTexts(ootDump, mmDump, "displayName");
}

void LogFromGame(const char* game, FleetLogLevel level, const char* message) {
    std::cerr << "[" << game << "] " << message << "\n";
}

void* HostFunction(const char* name) {
    return std::string(name) == "Log" ? reinterpret_cast<void*>(LogFromGame) : nullptr;
}

template <GameIndex Game> struct Oracle {
    static const GameModule& Module() {
        return *sGames[Game];
    }

    static void Reset() {
        Ship::GameScope scope(Module().Name());
        Module().Functions().RandoReset();
    }

    static void SetOwnedItems(const char* itemsJson) {
        Ship::GameScope scope(Module().Name());
        Module().Functions().RandoSetOwnedItems(itemsJson);
    }

    // The fill reads the result before the next query, so one buffer per game is enough.
    static const char* GetReachableChecks() {
        static std::string reachable;
        Ship::GameScope scope(Module().Name());
        reachable = Module().TakeString(Module().Functions().RandoGetReachableChecks());
        return reachable.c_str();
    }

    static void PlaceItem(const char* check, const char* item) {
        Ship::GameScope scope(Module().Name());
        Module().Functions().RandoPlaceItem(check, item);
    }

    static uint8_t GetPortalOpen() {
        const nlohmann::json reply = nlohmann::json::parse(Module().Request("GetPortalOpen"), nullptr, false);
        return reply.is_object() && reply.value("open", false) ? 1 : 0;
    }

    static std::string DumpStaticData() {
        Ship::GameScope scope(Module().Name());
        return Module().TakeString(Module().Functions().DumpRandoStaticData());
    }
};

template <GameIndex Game> struct CrossHost {
    static const char* Reply(const std::string& op, const std::string& json) {
        static std::string reply;
        reply = sGames[Game]->Request(op, json);
        return reply.empty() ? nullptr : reply.c_str();
    }

    static const char* DumpCatalogue() {
        return Reply("DumpCrossDungeonCatalogue", "{}");
    }

    static const char* DumpCatalogueForOptions(const char* options) {
        return Reply("DumpCrossDungeonCatalogue", options);
    }

    static const char* DumpSeedCatalogue(const char* spoiler) {
        return Reply("DumpCrossDungeonCatalogueForSeed", spoiler);
    }

    static int SetSeed(const char* seed) {
        return Reply("SetCrossDungeonSeed", seed) != nullptr ? 1 : 0;
    }

    static void SetAccess(const char* access) {
        Reply("SetCrossDungeonAccess", access);
    }

    static const char* GetExits() {
        return Reply("GetCrossDungeonExits", "{}");
    }

    static uint16_t GetBossDefeated() {
        const char* reply = Reply("GetCrossBossDefeated", "{}");
        return reply != nullptr ? nlohmann::json::parse(reply).value("defeated", uint16_t{ 0 }) : 0;
    }

    static int CanEnterArrival(int32_t entrance) {
        const char* reply = Reply("CanEnterCrossEntrance", nlohmann::json{ { "entrance", entrance } }.dump());
        return reply != nullptr && nlohmann::json::parse(reply).value("allowed", false) ? 1 : 0;
    }

    static ComboRando::CrossDungeon::HostFns Fns() {
        ComboRando::CrossDungeon::HostFns fns;
        fns.DumpCatalogue = DumpCatalogue;
        fns.SetSeed = SetSeed;
        fns.SetAccess = SetAccess;
        fns.GetExits = GetExits;
        fns.DumpSeedCatalogue = DumpSeedCatalogue;
        fns.DumpCatalogueForOptions = DumpCatalogueForOptions;
        fns.GetBossDefeated = GetBossDefeated;
        if (Game == Mm) {
            fns.CanEnterArrival = CanEnterArrival;
        }
        return fns;
    }
};

ComboRando::CrossDungeon::Coordinator sCrossDungeons;

bool ParseTriforceHunt(const std::string& value, ComboRando::CwGoal& goal) {
    const size_t colon = value.find(':');
    const int required = std::atoi(value.substr(0, colon).c_str());
    const int total = colon == std::string::npos ? required : std::atoi(value.substr(colon + 1).c_str());
    if (required < 1 || total < required || total > ComboRando::kMaxComboTriforcePieces) {
        return false;
    }
    goal = { .hunt = true, .required = required, .total = total };
    return true;
}

bool ParseStartingGame(const std::string& value, StartingGame& start) {
    if (value == "oot" || value == "OOT") {
        start = StartingGame::Oot;
    } else if (value == "mm" || value == "MM") {
        start = StartingGame::Mm;
    } else if (value == "random") {
        start = StartingGame::Random;
    } else {
        return false;
    }
    return true;
}

bool RollsMmStart(uint32_t masterSeed) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : "startingGame:" + std::to_string(masterSeed)) {
        hash = (hash ^ c) * 16777619u;
    }
    return (hash & 1u) != 0;
}

std::vector<std::string> SplitCommaList(const std::string& value) {
    std::vector<std::string> names;
    std::stringstream list(value);
    std::string name;
    while (std::getline(list, name, ',')) {
        names.push_back(name);
    }
    return names;
}

bool ParseSharedItems(const std::string& value, uint64_t& mask) {
    if (value == "all") {
        mask = ComboRando::SharedMaskAll();
        return true;
    }
    mask = ComboRando::SharedMaskFromKeys(SplitCommaList(value));
    return mask != 0;
}

bool ParseCrossPools(const std::string& value, std::optional<ComboRando::CrossDungeon::PoolOptions>& pools) {
    const nlohmann::json known = ComboRando::CrossDungeon::OptionsJson({});
    nlohmann::json flags = { { "dungeons", false } };
    for (const auto& name : SplitCommaList(value)) {
        if (!known.contains(name)) {
            return false;
        }
        flags[name] = true;
    }
    pools = ComboRando::CrossDungeon::ParseOptions(flags);
    return pools->Any();
}

bool ParsePassages(const std::string& value, nlohmann::json& passages) {
    passages = nlohmann::json::array();
    for (const auto& name : SplitCommaList(value)) {
        if (ComboRando::CrossDungeon::FindPassage(name) == nullptr) {
            return false;
        }
        passages.push_back(name);
    }
    return !passages.empty();
}

bool ParseOptions(int argc, char* argv[], Options& options) {
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string flag = argv[i];
        const std::string value = argv[i + 1];
        if (flag == "--oot") {
            options.ootDirectory = value;
        } else if (flag == "--mm") {
            options.mmDirectory = value;
        } else if (flag == "--seed") {
            options.seed = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (flag == "--out") {
            options.outputPath = value;
        } else if (flag == "--settings") {
            options.settingsPath = value;
        } else if (flag == "--triforce") {
            if (!ParseTriforceHunt(value, options.goal)) {
                return false;
            }
        } else if (flag == "--start") {
            if (!ParseStartingGame(value, options.start)) {
                return false;
            }
        } else if (flag == "--cross") {
            if (!ParseCrossPools(value, options.crossPools)) {
                return false;
            }
        } else if (flag == "--test-chain") {
            options.testChain = value == "1";
        } else if (flag == "--zone-oot" || flag == "--zone-mm") {
            auto& zones = options.rules.zones;
            if (!zones) {
                zones = ComboRando::CwZones{ std::regex("$^"), std::regex("$^") };
            }
            (flag == "--zone-oot" ? zones->oot : zones->mm) = std::regex(value);
        } else if (flag == "--passages") {
            if (!ParsePassages(value, options.passages)) {
                return false;
            }
        } else if (flag == "--progress") {
            options.progressPath = value;
        } else if (flag == "--catalogue") {
            options.cataloguePath = value;
        } else if (flag == "--playthrough") {
            options.playthroughPath = value;
        } else if (flag == "--pool") {
            options.poolPath = value;
            options.poolOnly = true;
        } else if (flag == "--serve") {
            options.serveFolder = value;
        } else if (flag == "--dumps") {
            options.dumpDirectory = value;
        } else if (flag == "--plentiful-mod-items") {
            if (value != "true" && value != "false") {
                return false;
            }
            options.plentifulModItems = value == "true";
        } else if (flag == "--shared") {
            if (!ParseSharedItems(value, options.sharedMask)) {
                return false;
            }
        } else if (flag == "--unshared") {
            options.unsharedPairs = SplitCommaList(value);
        } else {
            return false;
        }
    }
    return !options.ootDirectory.empty() && !options.mmDirectory.empty();
}

bool AttachGames(const Options& options) {
    sGames[Oot] = GameModule::Load(options.ootDirectory, "soh", HostFunction);
    sGames[Mm] = GameModule::Load(options.mmDirectory, "2ship", HostFunction);
    if (sGames[Oot] == nullptr || sGames[Mm] == nullptr) {
        return false;
    }
    for (const auto& game : sGames) {
        game->Request("InitRandoHeadless");
    }
    sCrossDungeons.hosts = { CrossHost<Oot>::Fns(), CrossHost<Mm>::Fns() };
    return true;
}

const char* const kExcludedChecksKeys[GameCount] = { "gRandoSettings.ExcludedLocations", "gRando.ExcludedChecks" };

bool IsValidGoal(const ComboRando::CwGoal& goal) {
    if (!goal.hunt) {
        return goal.pieces >= 0 && goal.pieces <= ComboRando::kMaxComboTriforcePieces;
    }
    return goal.required >= 1 && goal.total >= goal.required && goal.total <= ComboRando::kMaxComboTriforcePieces;
}

void ApplyFleetBlock(const nlohmann::json& fleet, Options& options) {
    if (!fleet.is_object()) {
        return;
    }
    options.fleetBlock = fleet;
    const nlohmann::json goal = fleet.value("goal", nlohmann::json());
    if (goal.is_object()) {
        const bool hunt = goal.value("type", std::string("bosses")) == "triforceHunt";
        const ComboRando::CwGoal parsed = { .hunt = hunt,
                                            .required = hunt ? goal.value("required", 0) : 0,
                                            .total = hunt ? goal.value("total", goal.value("required", 0)) : -1,
                                            .either = goal.value("mode", std::string("both")) == "either",
                                            .pieces = hunt ? 0 : goal.value("pieces", 0) };
        if (IsValidGoal(parsed)) {
            options.goal = parsed;
        } else {
            std::cerr << "[fleet-seedgen] ignoring an invalid goal: " << goal.dump() << "\n";
        }
    }
    options.songs = fleet.value("songs", std::string("native"));
    std::optional<ComboRando::CwZones> zones = std::move(options.rules.zones);
    options.rules = ComboRando::ParseCwFillRules(fleet);
    options.rules.zones = std::move(zones);
}

enum KeyRingMode { kKeyRingsOff, kKeyRingsRandom, kKeyRingsCount, kKeyRingsSelection };
constexpr int kKeyRingYes = 2;
constexpr int kKeyRingNo = 0;
constexpr const char* kOotKeyRingMode = "gRandoSettings.ShuffleKeyRings";
constexpr const char* kMmKeyRingMode = "gRando.ModOptions.mm_keyrings.mode";

struct KeyRingDungeon {
    GameIndex game;
    const char* cvar;
};
constexpr KeyRingDungeon kKeyRingDungeons[] = {
    { Oot, "gRandoSettings.ShuffleKeyRingsForestTemple" },
    { Oot, "gRandoSettings.ShuffleKeyRingsFireTemple" },
    { Oot, "gRandoSettings.ShuffleKeyRingsWaterTemple" },
    { Oot, "gRandoSettings.ShuffleKeyRingsSpiritTemple" },
    { Oot, "gRandoSettings.ShuffleKeyRingsShadowTemple" },
    { Oot, "gRandoSettings.ShuffleKeyRingsBottomOfTheWell" },
    { Oot, "gRandoSettings.ShuffleKeyRingsGTG" },
    { Oot, "gRandoSettings.ShuffleKeyRingsGanonsCastle" },
    { Oot, "gRandoSettings.ShuffleKeyRingsGerudoFortress" },
    { Oot, "gRandoSettings.ShuffleKeyRingsChestGame" },
    { Mm, "gRando.ModOptions.mm_keyrings.woodfall" },
    { Mm, "gRando.ModOptions.mm_keyrings.snowhead" },
    { Mm, "gRando.ModOptions.mm_keyrings.great_bay" },
    { Mm, "gRando.ModOptions.mm_keyrings.stone_tower" },
};

bool IsKeyRingCandidate(const KeyRingDungeon& dungeon, const std::array<nlohmann::json, GameCount>& blocks) {
    const nlohmann::json& settings = blocks[dungeon.game].value("settings", nlohmann::json::object());
    if (dungeon.game == Mm) {
        return settings.contains(kMmKeyRingMode);
    }
    const std::string cvar = dungeon.cvar;
    if (cvar.ends_with("GerudoFortress")) {
        return settings.value("gRandoSettings.FortressCarpenters", 0) == 0 &&
               settings.value("gRandoSettings.GerudoKeys", 0) != 0;
    }
    if (cvar.ends_with("ChestGame")) {
        return settings.value("gRandoSettings.ShuffleChestMinigame", 0) != 0;
    }
    return true;
}

void DistributeKeyRings(std::array<nlohmann::json, GameCount>& blocks, const nlohmann::json& fleet, uint32_t seed) {
    const int mode = blocks[Oot].value("settings", nlohmann::json::object()).value(kOotKeyRingMode, 0);
    if (mode != kKeyRingsRandom && mode != kKeyRingsCount) {
        return;
    }
    std::vector<const KeyRingDungeon*> candidates;
    for (const KeyRingDungeon& dungeon : kKeyRingDungeons) {
        if (IsKeyRingCandidate(dungeon, blocks)) {
            candidates.push_back(&dungeon);
        }
    }
    ComboRando::CwRng rng(seed ^ 0x4B52494Eu);
    ComboRando::cwShuffle(candidates, rng);
    const size_t requested = fleet.is_object() ? fleet.value("keyRingCount", size_t{ 0 }) : 0;
    const size_t count = mode == kKeyRingsCount ? std::min(requested, candidates.size())
                                                : rng.below(static_cast<uint32_t>(candidates.size() + 1));
    for (size_t index = 0; index < candidates.size(); index++) {
        blocks[candidates[index]->game]["settings"][candidates[index]->cvar] = index < count ? kKeyRingYes : kKeyRingNo;
    }
    blocks[Oot]["settings"][kOotKeyRingMode] = kKeyRingsSelection;
    if (blocks[Mm]["settings"].contains(kMmKeyRingMode)) {
        blocks[Mm]["settings"][kMmKeyRingMode] = kKeyRingsSelection;
    }
}

void PoolRewardsForFleet(std::array<nlohmann::json, GameCount>& blocks, const nlohmann::json& fleet) {
    constexpr int kOotRewardsAnywhere = 5;
    constexpr int kOotPocketDungeonReward = 0;
    constexpr int kMmRemainsShuffled = 1;
    const std::string mode = fleet.is_object() ? fleet.value("rewards", std::string("native")) : "native";
    if (ComboRando::CwRewardPlacementFromName(mode) == ComboRando::CwRewardPlacement::NATIVE) {
        return;
    }
    blocks[Oot]["settings"]["gRandoSettings.ShuffleDungeonReward"] = kOotRewardsAnywhere;
    blocks[Oot]["settings"]["gRandoSettings.LinksPocket"] = kOotPocketDungeonReward;
    blocks[Mm]["settings"]["gRando.Options.RO_SHUFFLE_BOSS_REMAINS"] = kMmRemainsShuffled;
}

void ShuffleSharedGreatSpin(std::array<nlohmann::json, GameCount>& blocks, uint64_t sharedMask) {
    if ((sharedMask & (uint64_t{ 1 } << ComboRando::SF_GREAT_SPIN)) == 0) {
        return;
    }
    blocks[Oot]["settings"]["gRandoSettings.Mods.nei.shuffle.great_spin"] = 1;
}

bool RestoreSettingsFrom(const std::string& spoilerPath, Options& options, bool commandLineGoal) {
    const nlohmann::json spoiler = nlohmann::json::parse(std::ifstream(spoilerPath), nullptr, false);
    if (!spoiler.is_object()) {
        std::cerr << "[fleet-seedgen] could not read settings from " << spoilerPath << "\n";
        return false;
    }
    const nlohmann::json fleet = spoiler.value("fleet", nlohmann::json());
    const bool hasPlando = fleet.is_object() && fleet.value("plando", nlohmann::json()).is_array();
    std::array<nlohmann::json, GameCount> blocks;
    for (int game = 0; game < GameCount; game++) {
        blocks[game] = spoiler.value(kGameKeys[game], nlohmann::json::object());
        if (hasPlando) {
            blocks[game]["settings"][kExcludedChecksKeys[game]] =
                game == Oot ? nlohmann::json(std::string{}) : nlohmann::json::array();
        }
    }
    if (options.sharedMask == 0) {
        options.sharedMask = ComboRando::SharedMaskFromKeys(spoiler.value("sharedItems", nlohmann::json::array()));
    }
    DistributeKeyRings(blocks, fleet, options.seed);
    PoolRewardsForFleet(blocks, fleet);
    ShuffleSharedGreatSpin(blocks, options.sharedMask);
    for (int game = 0; game < GameCount; game++) {
        sGames[game]->Request("RestoreSettings", blocks[game].dump());
    }
    const nlohmann::json goal = spoiler.value("goal", nlohmann::json::object());
    if (!commandLineGoal && goal.value("type", std::string()) == "triforceHunt") {
        options.goal = { .hunt = true,
                         .required = goal.value("requiredPieces", 0),
                         .total = goal.value("totalPieces", -1) };
    } else if (!commandLineGoal) {
        options.goal.either = goal.value("mode", std::string("both")) == "either";
        options.goal.pieces = goal.value("pieces", 0);
    }
    ApplyFleetBlock(fleet, options);
    if (!commandLineGoal) {
        ParseStartingGame(spoiler.value("startingGame", std::string("OOT")), options.start);
    }
    if (options.unsharedPairs.empty()) {
        options.unsharedPairs = spoiler.value("unsharedPairs", std::vector<std::string>{});
    }
    const nlohmann::json crossSeed = ComboRando::CrossDungeon::Coordinator::SeedOf(spoiler);
    if (!options.crossPools && ComboRando::CrossDungeon::Coordinator::HasRoutes(crossSeed)) {
        options.crossPools = ComboRando::CrossDungeon::OptionsFromSeed(crossSeed);
    }
    if (options.passages.empty() && crossSeed.is_object()) {
        options.passages = crossSeed.value("passages", nlohmann::json::array());
    }
    return true;
}

void SettleTriforcePair(Options& options) {
    const std::string key = ComboRando::kTriforcePairKey;
    std::erase(options.unsharedPairs, key);
    if (options.goal.hunt) {
        options.unsharedPairs.push_back(key);
    }
}

void SendComboOptions(const Options& options, bool mmStart) {
    const std::string startingGame = mmStart ? "MM" : "OOT";
    const ComboRando::CwGoal& goal = options.goal;
    const int poolPieces = ComboRando::CwPoolPieces(goal);
    const int pieces[GameCount] = { ComboRando::CwOotPieces(poolPieces), ComboRando::CwMmPieces(poolPieces) };
    for (int game = 0; game < GameCount; game++) {
        const nlohmann::json request = { { "hunt", goal.hunt },
                                         { "required", goal.required },
                                         { "pieces", pieces[game] },
                                         { "totalPieces", poolPieces },
                                         { "startingGame", startingGame },
                                         { "songs", options.songs },
                                         { "goalMode", goal.either ? "either" : "both" } };
        sGames[game]->Request("SetComboOptions", request.dump());
    }
}

const ComboRando::OracleFns kOotOracle = { .Reset = Oracle<Oot>::Reset,
                                           .SetOwnedItems = Oracle<Oot>::SetOwnedItems,
                                           .GetReachableChecks = Oracle<Oot>::GetReachableChecks,
                                           .PlaceItem = Oracle<Oot>::PlaceItem,
                                           .GetPortalOpen = Oracle<Oot>::GetPortalOpen };
const ComboRando::OracleFns kMmOracle = { .Reset = Oracle<Mm>::Reset,
                                          .SetOwnedItems = Oracle<Mm>::SetOwnedItems,
                                          .GetReachableChecks = Oracle<Mm>::GetReachableChecks,
                                          .PlaceItem = Oracle<Mm>::PlaceItem };

void WriteDumps(const Fill& fill, const std::string& directory) {
    if (directory.empty()) {
        return;
    }
    std::filesystem::create_directories(directory);
    std::ofstream(std::filesystem::path(directory) / "oot.json") << fill.ootDump;
    std::ofstream(std::filesystem::path(directory) / "mm.json") << fill.mmDump;
    std::ofstream(std::filesystem::path(directory) / "oot-hints.json") << fill.ootHintDump;
    std::ofstream(std::filesystem::path(directory) / "fill.json") << fill.result.spoilerJson;
}

ComboRando::RequirednessResult FindRequiredItems(const Fill& fill, const Options& options, bool noLogic) {
    if (options.goal.hunt && noLogic) {
        return {};
    }
    if (!ComboRando::NeedsRequirednessPareDown(fill.ootHintDump, fill.mmDump)) {
        return {};
    }
    std::unordered_map<std::string, std::string> ootAreas;
    const nlohmann::json ootHints = nlohmann::json::parse(fill.ootHintDump, nullptr, false);
    for (const nlohmann::json& check : ootHints.value("checks", nlohmann::json::array())) {
        ootAreas.emplace(check.value("name", ""), check.value("area", ""));
    }
    std::unordered_map<std::string, std::string> mmAreas;
    const nlohmann::json mmLocationHints =
        nlohmann::json::parse(fill.mmDump).value("locationHints", nlohmann::json::object());
    for (const auto& [check, area] : mmLocationHints.items()) {
        mmAreas.emplace(check, area.get<std::string>());
    }
    ComboRando::GoalPredicate goal = ComboRando::DefaultGanonMajoraGoal;
    if (options.goal.hunt) {
        goal = ComboRando::MakeTriforceHuntGoal(options.goal.required);
    } else if (noLogic) {
        goal = ComboRando::MmOnlyMajoraGoal;
    } else if (options.goal.either) {
        goal = ComboRando::EitherGanonMajoraGoal;
    }
    return ComboRando::PareDownPlaythrough(fill.result.spoilerJson, fill.ootOracle, fill.mmOracle, nullptr,
                                           fill.ootDump, fill.mmDump, ootAreas, mmAreas, goal, !noLogic, fill.mmStart,
                                           nullptr, options.sharedMask,
                                           ComboRando::EncodeUnsharedSpec(options.unsharedPairs));
}

bool LayOutCrossEntrances(Fill& fill, const Options& options) {
    const bool enabled = options.crossPools.has_value();
    if (enabled) {
        sCrossDungeons.SetOptions(*options.crossPools);
    }
    sCrossDungeons.testChain = options.testChain;
    try {
        fill.crossDungeonSeed =
            ComboRando::CrossDungeon::GenerateValidatedSeed(sCrossDungeons, enabled, fill.masterSeed, fill.ootDump,
                                                            fill.mmDump, fill.ootOracle, fill.mmOracle, fill.mmStart);
        if (!options.passages.empty()) {
            if (!fill.crossDungeonSeed.is_object()) {
                fill.crossDungeonSeed = { { "enabled", false } };
            }
            fill.crossDungeonSeed["passages"] = options.passages;
            sCrossDungeons.InstallLogic(fill.crossDungeonSeed, false);
            sCrossDungeons.ConfigureOracles(fill.ootOracle, fill.mmOracle);
        }
    } catch (const std::exception& e) {
        fill.result.error = e.what();
        return false;
    }
    return true;
}

void ReportProgress(const Options& options, const char* phase) {
    if (!options.progressPath.empty()) {
        std::ofstream(options.progressPath) << nlohmann::json{ { "phase", phase } }.dump();
    }
}

const char* const kMmStartingItemsKey = "gRando.StartingItems";
const char* const kFleetStartingItemsKey = "fleet.startingItems";

bool StartsInOot(const std::string& name) {
    const nlohmann::json native = nlohmann::json::parse(sGames[Oot]->Request("StartingItems"), nullptr, false);
    const auto isChosen = [&](const nlohmann::json& item) {
        return item.value("name", std::string{}) == name && item.value("count", 0) > 0;
    };
    if (native.is_array() && std::any_of(native.begin(), native.end(), isChosen)) {
        return true;
    }
    const nlohmann::json block = nlohmann::json::parse(sGames[Oot]->Request("DumpSettings"), nullptr, false);
    if (!block.is_object()) {
        return false;
    }
    const nlohmann::json fleetOwn =
        block.value("settings", nlohmann::json::object()).value(kFleetStartingItemsKey, nlohmann::json::array());
    return std::any_of(fleetOwn.begin(), fleetOwn.end(), [&](const nlohmann::json& entry) {
        return entry.is_string() && entry.get<std::string>() == name;
    });
}

std::set<std::string> FindWithheldMmTokens(const std::string& mmDump) {
    std::set<std::string> tokens;
    const nlohmann::json items = nlohmann::json::parse(mmDump).value("items", nlohmann::json::array());
    for (const ComboRando::SharedFamily family : ComboRando::kMmEntryFamilies) {
        const ComboRando::SharedFamilyDef& def = ComboRando::SharedFamilyByIndex(family);
        if (StartsInOot(def.ootName)) {
            continue;
        }
        for (const nlohmann::json& item : items) {
            if (item.value("name", std::string{}) == def.mmName) {
                tokens.insert(item.value("token", std::string{}));
            }
        }
    }
    return tokens;
}

bool WithholdMmEntryItems(const nlohmann::json& mmSettings, const std::string& mmDump) {
    const nlohmann::json starting =
        mmSettings.value("settings", nlohmann::json::object()).value(kMmStartingItemsKey, nlohmann::json::array());
    const std::set<std::string> withheld = FindWithheldMmTokens(mmDump);
    nlohmann::json kept = nlohmann::json::array();
    for (const nlohmann::json& token : starting) {
        if (!token.is_string() || withheld.count(token.get<std::string>()) == 0) {
            kept.push_back(token);
        }
    }
    if (kept.size() == starting.size()) {
        return false;
    }
    nlohmann::json patched = mmSettings;
    patched["settings"][kMmStartingItemsKey] = std::move(kept);
    sGames[Mm]->Request("RestoreSettings", patched.dump());
    return true;
}

Fill PrepareWorlds(uint32_t masterSeed, const Options& options, bool mmStart, const nlohmann::json& mmSettings) {
    ReportProgress(options, "Preparing the OoT and MM worlds...");
    sGames[Mm]->Request("RestoreSettings", mmSettings.dump());
    const std::string seed = nlohmann::json{ { "seed", masterSeed } }.dump();
    for (const auto& game : sGames) {
        game->Request("SetSeed", seed);
    }
    SendComboOptions(options, mmStart);
    Fill fill = { .masterSeed = masterSeed,
                  .mmStart = mmStart,
                  .ootDump = Oracle<Oot>::DumpStaticData(),
                  .mmDump = Oracle<Mm>::DumpStaticData(),
                  .ootOracle = kOotOracle,
                  .mmOracle = kMmOracle };
    if (fill.ootDump.empty() || fill.mmDump.empty()) {
        fill.result.error = "a game could not prepare its seed (see its log)";
        return fill;
    }
    if (!mmStart && WithholdMmEntryItems(mmSettings, fill.mmDump)) {
        fill.mmDump = Oracle<Mm>::DumpStaticData();
    }
    return fill;
}

Fill FillOnce(uint32_t masterSeed, const Options& options, bool mmStart, const nlohmann::json& mmSettings) {
    Fill fill = PrepareWorlds(masterSeed, options, mmStart, mmSettings);
    if (fill.ootDump.empty() || fill.mmDump.empty()) {
        return fill;
    }
    const ComboRando::CwGoal& goal = options.goal;
    if (goal.hunt && ComboRando::CountPoolTriforcePieces(fill.ootDump, fill.mmDump) < goal.required) {
        fill.result.error = "the combined pool holds fewer Triforce pieces than the hunt requires";
        return fill;
    }
    const std::string forcedOot = sGames[Oot]->Request("GetForcedPlacements");
    if (!options.poolOnly) {
        ReportProgress(options, "Connecting entrances between both games...");
        if (!LayOutCrossEntrances(fill, options)) {
            return fill;
        }
    }
    const ComboRando::OotAccess ootAccess = ComboRando::OotAccessFromDump(fill.ootDump);
    ReportProgress(options, "Placing items and validating logic...");
    fill.result = ComboRando::CrossWorldCombinedFill(
        fill.ootDump, fill.mmDump, masterSeed, fill.ootOracle, fill.mmOracle, nullptr, forcedOot, ootAccess, goal,
        mmStart ? ComboRando::GAME_MM : ComboRando::GAME_OOT, ComboRando::EncodeUnsharedSpec(options.unsharedPairs),
        options.sharedMask, options.poolOnly, options.plentifulModItems, options.rules);
    if (fill.result.success) {
        if (!options.poolOnly) {
            fill.ootHintDump = sGames[Oot]->Request("DumpHintData");
        }
        WriteDumps(fill, options.dumpDirectory);
    }
    if (fill.result.success && !options.poolOnly) {
        ReportProgress(options, "Generating hints...");
        fill.requiredness = FindRequiredItems(fill, options, ootAccess == ComboRando::OotAccess::NO_LOGIC);
    }
    return fill;
}

nlohmann::json GoalJson(const ComboRando::CwGoal& goal) {
    const char* mode = goal.either ? "either" : "both";
    if (!goal.hunt) {
        return { { "type", "bosses" }, { "mode", mode }, { "pieces", goal.pieces } };
    }
    return {
        { "type", "triforceHunt" }, { "mode", mode }, { "requiredPieces", goal.required }, { "totalPieces", goal.total }
    };
}

nlohmann::json BuildSpoiler(const Fill& fill, const Options& options) {
    const nlohmann::json fillSpoiler = nlohmann::json::parse(fill.result.spoilerJson);
    const nlohmann::json sharedItems = fillSpoiler.value("sharedItems", nlohmann::json::array());
    const uint64_t sharedMask = ComboRando::SharedMaskFromKeys(sharedItems);
    const auto modPairs = ComboRando::ResolveSharedPairs(ComboRando::EncodeUnsharedSpec(options.unsharedPairs),
                                                         fill.mmDump, fill.ootDump);
    nlohmann::json placements[GameCount] = { fillSpoiler.value("oot", nlohmann::json::object()),
                                             fillSpoiler.value("mm", nlohmann::json::object()) };
    const nlohmann::json rawForeign = fillSpoiler.value("foreign", nlohmann::json::array());
    ComboRando::SuffixCrossGameItems(placements[Oot], placements[Mm], rawForeign, fill.ootDump, fill.mmDump,
                                     ComboRando::SharedUntaggedNames(sharedMask, modPairs));
    nlohmann::json disguised = rawForeign;
    ComboRando::AssignTrapDisguises(disguised, fillSpoiler.value("oot", nlohmann::json::object()),
                                    fillSpoiler.value("mm", nlohmann::json::object()), fill.ootDump, fill.mmDump,
                                    fill.masterSeed);
    nlohmann::json foreign = ComboRando::BuildForeignArray(disguised, sharedMask, modPairs);
    const auto labels = ItemLabels(fill.ootDump, fill.mmDump);
    for (auto& entry : foreign) {
        const auto label =
            labels.find({ entry.value("itemGame", std::string{}), entry.value("itemName", std::string{}) });
        if (label != labels.end()) {
            entry["label"] = label->second;
        }
    }

    nlohmann::json spoiler = { { "fileType", "ComboShipRandomizer" },
                               { "seed", options.seed },
                               { "masterSeed", fill.masterSeed },
                               { "goal", GoalJson(options.goal) },
                               { "startingGame", fill.mmStart ? "MM" : "OOT" },
                               { "sharedItems", sharedItems },
                               { "unsharedPairs", options.unsharedPairs },
                               { "modFamilies", ComboRando::EncodeModPairs(modPairs) },
                               { "plentifulModItems", fillSpoiler.value("plentifulModItems", false) },
                               { "foreign", foreign },
                               { "hints",
                                 ComboRando::Generate(fill.masterSeed, fill.ootDump, fill.ootHintDump, fill.mmDump,
                                                      foreign, fill.result.spoilerJson, fill.requiredness) } };
    const std::string dumps[GameCount] = { fill.ootDump, fill.mmDump };
    for (int game = 0; game < GameCount; game++) {
        nlohmann::json block = nlohmann::json::parse(sGames[game]->Request("DumpSettings"), nullptr, false);
        if (!block.is_object()) {
            block = nlohmann::json::object();
        }
        block["placements"] = std::move(placements[game]);
        block["prices"] = nlohmann::json::parse(dumps[game]).value("prices", nlohmann::json::object());
        spoiler[kGameKeys[game]] = std::move(block);
    }
    if (ComboRando::CrossDungeon::Coordinator::Enabled(fill.crossDungeonSeed)) {
        spoiler["crossDungeonEntrances"] = fill.crossDungeonSeed;
    }
    spoiler["songs"] = options.songs;
    if (options.fleetBlock.is_object()) {
        spoiler["fleet"] = options.fleetBlock;
    }
    spoiler["entrances"] = { { "oot", spoiler["oot"].value("entrances", nlohmann::json::array()) } };
    spoiler["oot"].erase("entrances");
    return spoiler;
}

nlohmann::json PlaythroughPlacements(const nlohmann::json& spoiler, const std::string dumps[GameCount]) {
    nlohmann::json flat = { { "foreign", spoiler.value("foreign", nlohmann::json::array()) } };
    for (int game = 0; game < GameCount; game++) {
        nlohmann::json placements =
            spoiler.value(kGameKeys[game], nlohmann::json::object()).value("placements", nlohmann::json::object());
        const nlohmann::json fixed = nlohmann::json::parse(dumps[game]).value("fixed", nlohmann::json::array());
        for (const nlohmann::json& entry : fixed) {
            const std::string check = entry.value("check", "");
            if (!check.empty() && !placements.contains(check)) {
                placements[check] = entry.value("item", "");
            }
        }
        flat[kGameKeys[game]] = std::move(placements);
    }
    return flat;
}

int ValidateSpoiler(Options& options) {
    const nlohmann::json spoiler = nlohmann::json::parse(std::ifstream(options.playthroughPath), nullptr, false);
    if (!spoiler.is_object() || !RestoreSettingsFrom(options.playthroughPath, options, false)) {
        std::cerr << "[fleet-seedgen] could not read " << options.playthroughPath << "\n";
        return 2;
    }
    SettleTriforcePair(options);
    const uint32_t masterSeed = spoiler.value("masterSeed", 0u);
    const bool mmStart = spoiler.value("startingGame", std::string("OOT")) == "MM";
    const std::string seed = nlohmann::json{ { "seed", masterSeed } }.dump();
    for (const auto& game : sGames) {
        game->Request("SetSeed", seed);
    }
    SendComboOptions(options, mmStart);
    const std::string dumps[GameCount] = { Oracle<Oot>::DumpStaticData(), Oracle<Mm>::DumpStaticData() };
    for (int game = 0; game < GameCount; game++) {
        const nlohmann::json prices =
            spoiler.value(kGameKeys[game], nlohmann::json::object()).value("prices", nlohmann::json::object());
        sGames[game]->Request("SetCheckPrices", prices.dump());
    }
    ComboRando::OracleFns ootOracle = kOotOracle;
    ComboRando::OracleFns mmOracle = kMmOracle;
    const nlohmann::json crossSeed = ComboRando::CrossDungeon::Coordinator::SeedOf(spoiler);
    if (options.crossPools) {
        sCrossDungeons.SetOptions(*options.crossPools);
    }
    ComboRando::CrossDungeon::ValidateSavedStructure(sCrossDungeons, crossSeed, dumps[Oot], dumps[Mm], ootOracle,
                                                     mmOracle, mmStart);

    const bool portalGated = ComboRando::OotAccessFromDump(dumps[Oot]) != ComboRando::OotAccess::NO_LOGIC;
    const ComboRando::PlaythroughResult result = ComboRando::RunPlaythrough(
        PlaythroughPlacements(spoiler, dumps).dump(), ootOracle, mmOracle, std::to_string(masterSeed), nullptr, nullptr,
        dumps[Oot], dumps[Mm], portalGated, false, options.goal, mmStart, options.sharedMask,
        ComboRando::EncodeUnsharedSpec(options.unsharedPairs));
    sGames[Mm]->Request("Finish");
    std::cout << "[fleet-seedgen] " << options.playthroughPath << (result.beatable ? " BEATABLE" : " NOT BEATABLE")
              << ": OoT " << result.reachableOot << " reachable / " << result.unreachableOot << " not, MM "
              << result.reachableMm << " / " << result.unreachableMm << "\n";
    return result.beatable ? 0 : 1;
}

bool ResolvesMmStart(StartingGame start, uint32_t masterSeed, int attempt) {
    if (start == StartingGame::Random) {
        return attempt + 1 < ComboRando::kFillAttempts && RollsMmStart(masterSeed);
    }
    return start == StartingGame::Mm;
}

constexpr int kPrepareAttempts = 8;

Fill FillWithRetries(const Options& options) {
    Fill fill = {};
    const nlohmann::json mmSettings = nlohmann::json::parse(sGames[Mm]->Request("DumpSettings"), nullptr, false);
    if (!mmSettings.is_object()) {
        fill.result.error = "MM could not report its settings (see its log)";
        return fill;
    }
    int fills = 0;
    for (int attempt = 0; attempt < kPrepareAttempts && fills < ComboRando::kFillAttempts && !fill.result.success;
         attempt++) {
        const uint32_t masterSeed = options.seed + attempt * 0x9E3779B9u;
        fill = FillOnce(masterSeed, options, ResolvesMmStart(options.start, masterSeed, fills), mmSettings);
        if (!fill.ootDump.empty() && !fill.mmDump.empty()) {
            fills++;
        }
    }
    return fill;
}

void AddItemTexts(nlohmann::json& items, const Fill& fill) {
    constexpr std::array<const char*, 4> kFields = { "displayName", "description", "variantOf", "variantOfName" };
    std::array<ItemTextMap, kFields.size()> texts;
    for (size_t field = 0; field < kFields.size(); field++) {
        texts[field] = ReadItemTexts(fill.ootDump, fill.mmDump, kFields[field]);
    }
    for (auto& entry : items) {
        const std::pair<std::string, std::string> key = { entry.value("game", std::string{}),
                                                          entry.value("name", std::string{}) };
        for (size_t field = 0; field < kFields.size(); field++) {
            if (const auto text = texts[field].find(key); text != texts[field].end()) {
                entry[kFields[field]] = text->second;
            }
        }
        const auto owner = texts[0].find({ key.first, entry.value("variantOf", std::string{}) });
        if (owner != texts[0].end()) {
            entry["variantOfName"] = owner->second;
        }
    }
}

std::string ReadOwnLabel(const ItemTextMap& labels, const nlohmann::json& pair, const char* game) {
    const std::string name = pair.value(game, std::string{});
    const auto found = labels.find({ game, name });
    const std::string label = found != labels.end() ? found->second : name;
    return label == pair.value("key", std::string{}) ? std::string{} : label;
}

nlohmann::json DescribeModPairs(const Fill& fill) {
    const ItemTextMap labels = ItemLabels(fill.ootDump, fill.mmDump);
    nlohmann::json pairs = ComboRando::EncodeModPairs(ComboRando::ResolveModPairs(fill.ootDump, fill.mmDump));
    for (auto& pair : pairs) {
        const std::string ootLabel = ReadOwnLabel(labels, pair, kGameKeys[Oot]);
        const std::string mmLabel = ReadOwnLabel(labels, pair, kGameKeys[Mm]);
        const std::string twinLabel = ootLabel.empty() ? mmLabel : ootLabel;
        const std::string readable = twinLabel.empty() ? pair.value("key", std::string{}) : twinLabel;
        pair["ootLabel"] = ootLabel.empty() ? readable : ootLabel;
        pair["mmLabel"] = mmLabel.empty() ? readable : mmLabel;
    }
    return pairs;
}

nlohmann::json DescribeSharePairs(const Fill& fill) {
    nlohmann::json pairs = DescribeModPairs(fill);
    const auto isTriforcePair = [](const nlohmann::json& pair) {
        return pair.value("key", std::string{}) == ComboRando::kTriforcePairKey;
    };
    pairs.erase(std::remove_if(pairs.begin(), pairs.end(), isTriforcePair), pairs.end());
    return pairs;
}

void FinishGames() {
    for (const auto& game : sGames) {
        game->Request("Finish");
    }
}

std::optional<nlohmann::json> ReadMmSettings() {
    nlohmann::json settings = nlohmann::json::parse(sGames[Mm]->Request("DumpSettings"), nullptr, false);
    if (!settings.is_object()) {
        return std::nullopt;
    }
    return std::make_optional(std::move(settings));
}

template <typename Attempt> Fill PrepareFirstWorkableSeed(const Options& options, Attempt attempt) {
    static std::optional<uint32_t> lastWorkableSeed;
    std::vector<uint32_t> candidates;
    if (lastWorkableSeed) {
        candidates.push_back(*lastWorkableSeed);
    }
    for (int index = 0; index < kPrepareAttempts; index++) {
        candidates.push_back(options.seed + index * 0x9E3779B9u);
    }
    Fill fill = {};
    for (const uint32_t masterSeed : candidates) {
        fill = attempt(masterSeed, ResolvesMmStart(options.start, masterSeed, 0));
        if (!fill.ootDump.empty() && !fill.mmDump.empty()) {
            lastWorkableSeed = masterSeed;
            return fill;
        }
    }
    return fill;
}

nlohmann::json ErrorReply(const std::string& error) {
    return { { "items", nlohmann::json::array() }, { "pairs", nlohmann::json::array() }, { "error", error } };
}

nlohmann::json BuildPoolReply(const Options& options) {
    const std::optional<nlohmann::json> mmSettings = ReadMmSettings();
    if (!mmSettings) {
        return ErrorReply("MM could not report its settings (see its log)");
    }
    Fill fill = PrepareFirstWorkableSeed(options, [&](uint32_t masterSeed, bool mmStart) {
        return FillOnce(masterSeed, options, mmStart, *mmSettings);
    });
    FinishGames();
    AddItemTexts(fill.result.pool, fill);
    nlohmann::json reply = { { "items", fill.result.pool }, { "pairs", DescribeSharePairs(fill) } };
    if (!fill.result.success) {
        reply["error"] = fill.result.error;
    }
    return reply;
}

int WritePool(const Options& options) {
    const nlohmann::json reply = BuildPoolReply(options);
    std::ofstream file(options.poolPath);
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file << reply.dump(2);
    return reply.contains("error") ? 1 : 0;
}

nlohmann::json DescribeChecks(const std::string& dumpJson, const nlohmann::json& forced) {
    const nlohmann::json dump = nlohmann::json::parse(dumpJson, nullptr, false);
    nlohmann::json checks = nlohmann::json::array();
    for (const nlohmann::json& check : dump.value("checks", nlohmann::json::array())) {
        checks.push_back({ { "name", check.value("name", std::string{}) },
                           { "area", check.value("area", std::string{}) },
                           { "type", check.value("type", std::string{}) },
                           { "vanilla", check.value("vanilla", std::string{}) } });
    }
    for (const auto& [name, spec] : forced.items()) {
        checks.push_back({ { "name", name },
                           { "area", name },
                           { "type", "" },
                           { "vanilla", spec.is_object() ? spec.value("item", std::string{}) : std::string{} } });
    }
    return checks;
}

nlohmann::json DescribePlandoItems(const Fill& fill) {
    const std::string* dumps[GameCount] = { &fill.ootDump, &fill.mmDump };
    std::map<std::pair<std::string, std::string>, bool> junkByItem;
    for (int game = 0; game < GameCount; game++) {
        const nlohmann::json dump = nlohmann::json::parse(*dumps[game], nullptr, false);
        for (const auto& [list, field] : { std::pair{ "pool", "name" }, std::pair{ "fixed", "item" } }) {
            for (const nlohmann::json& entry : dump.value(list, nlohmann::json::array())) {
                const std::string name = entry.value(field, std::string{});
                const bool isJunk =
                    entry.value("category", std::string{}) == "junk" && !entry.value("advancement", true);
                if (!name.empty()) {
                    junkByItem.emplace(std::make_pair(kGameKeys[game], name), isJunk);
                }
            }
        }
    }
    const ItemTextMap labels = ItemLabels(fill.ootDump, fill.mmDump);
    nlohmann::json items = nlohmann::json::array();
    for (const auto& [key, isJunk] : junkByItem) {
        const auto label = labels.find(key);
        items.push_back({ { "game", key.first },
                          { "name", key.second },
                          { "displayName", label != labels.end() ? label->second : key.second },
                          { "junk", isJunk } });
    }
    return items;
}

nlohmann::json BuildChecksReply(const Options& options) {
    const std::optional<nlohmann::json> mmSettings = ReadMmSettings();
    if (!mmSettings) {
        return { { "error", "MM could not report its settings (see its log)" } };
    }
    const Fill fill = PrepareFirstWorkableSeed(options, [&](uint32_t masterSeed, bool mmStart) {
        return PrepareWorlds(masterSeed, options, mmStart, *mmSettings);
    });
    const nlohmann::json forced = nlohmann::json::parse(sGames[Oot]->Request("GetForcedPlacements"), nullptr, false);
    FinishGames();
    if (fill.ootDump.empty() || fill.mmDump.empty()) {
        return { { "error", fill.result.error } };
    }
    return { { "oot", DescribeChecks(fill.ootDump, forced.is_object() ? forced : nlohmann::json::object()) },
             { "mm", DescribeChecks(fill.mmDump, nlohmann::json::object()) },
             { "items", DescribePlandoItems(fill) } };
}

void ResetGames(const std::array<nlohmann::json, GameCount>& baseline) {
    FinishGames();
    sCrossDungeons.InstallLogic(nullptr, false);
    sCrossDungeons.SetOptions({});
    sCrossDungeons.testChain = false;
    for (int game = 0; game < GameCount; game++) {
        sGames[game]->Request("RestoreSettings", baseline[game].dump());
    }
}

nlohmann::json AnswerRequest(const nlohmann::json& request, const Options& served,
                             const std::array<nlohmann::json, GameCount>& baseline) {
    std::vector<std::string> arguments = { "fleet", "--oot", served.ootDirectory, "--mm", served.mmDirectory };
    for (const nlohmann::json& argument : request.value("arguments", nlohmann::json::array())) {
        if (argument.is_string() && argument != "--internal-worker") {
            arguments.push_back(argument.get<std::string>());
        }
    }
    std::vector<char*> argv;
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    Options options;
    if (!ParseOptions(static_cast<int>(argv.size()), argv.data(), options)) {
        return ErrorReply("the pool request has unknown generator arguments");
    }
    options.ootDirectory = served.ootDirectory;
    options.mmDirectory = served.mmDirectory;
    options.settingsPath = request.value("settings", options.settingsPath);
    options.poolOnly = true;
    options.poolPath.clear();
    options.outputPath.clear();
    options.progressPath.clear();
    options.dumpDirectory.clear();
    const bool commandLineGoal = options.goal.hunt || options.start != StartingGame::Oot;
    ResetGames(baseline);
    if (!options.settingsPath.empty() && !RestoreSettingsFrom(options.settingsPath, options, commandLineGoal)) {
        return ErrorReply("could not read " + options.settingsPath);
    }
    SettleTriforcePair(options);
    const std::string op = request.value("op", std::string{});
    if (op == "pool") {
        return BuildPoolReply(options);
    }
    if (op == "checks") {
        return BuildChecksReply(options);
    }
    return ErrorReply("unknown pool worker request '" + op + "'");
}

void WriteReply(const std::filesystem::path& folder, const nlohmann::json& reply) {
    for (int attempt = 0; attempt < 100; attempt++) {
        if (ServeProtocol::TryWriteJson(folder / ServeProtocol::kReplyFile, reply)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::cerr << "[fleet-seedgen] could not write the reply to request " << reply.value("id", 0) << "\n";
}

int Serve(const Options& served) {
    const std::filesystem::path folder = served.serveFolder;
    std::filesystem::create_directories(folder);
    std::array<nlohmann::json, GameCount> baseline;
    for (int game = 0; game < GameCount; game++) {
        baseline[game] = nlohmann::json::parse(sGames[game]->Request("DumpSettings"), nullptr, false);
        if (!baseline[game].is_object()) {
            baseline[game] = nlohmann::json::object();
        }
    }
    if (!ServeProtocol::TryWriteJson(folder / ServeProtocol::kReadyFile, { { "ready", true } })) {
        std::cerr << "[fleet-seedgen] could not announce the warm worker in " << folder << "\n";
        return 2;
    }
    uint64_t answered = 0;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const nlohmann::json request = ServeProtocol::ReadJson(folder / ServeProtocol::kRequestFile);
        if (!request.is_object() || request.value("id", uint64_t{ 0 }) <= answered) {
            continue;
        }
        answered = request.value("id", uint64_t{ 0 });
        nlohmann::json reply;
        try {
            reply = AnswerRequest(request, served, baseline);
        } catch (const std::exception& error) { reply = ErrorReply(error.what()); }
        reply["id"] = answered;
        WriteReply(folder, reply);
        std::cout.flush();
        std::cerr.flush();
    }
}

int GenerateSeed(int argc, char* argv[]) {
    Options options;
    if (!ParseOptions(argc, argv, options)) {
        std::cerr << "usage: fleet-seedgen --oot <Ship folder> --mm <2Ship folder> [--seed N] [--out spoiler.json]\n"
                     "                     [--settings spoiler.json] [--triforce required[:total]]\n"
                     "                     [--start oot|mm|random] [--shared all|bows,wallets,...]\n"
                     "                     [--unshared modItemKey,...]\n"
                     "                     [--cross dungeons,expanded,interiors,shops,doorInteriors,grottos]\n"
                     "                     [--dumps <folder>]\n"
                     "       fleet-seedgen --oot <Ship folder> --mm <2Ship folder> --playthrough spoiler.json\n";
        return 2;
    }
    const bool commandLineGoal = options.goal.hunt || options.start != StartingGame::Oot;

    Ship::Context* context = Ship::Context::CreateUninitializedInstance("Fleet", "fleet", "fleetship.json");
    context->InitLogging();
    context->InitConfiguration();
    context->InitConsoleVariables();
    context->InitConsole();
    if (!AttachGames(options)) {
        return 2;
    }
    if (!options.serveFolder.empty()) {
        return Serve(options);
    }
    if (!options.cataloguePath.empty()) {
        nlohmann::json catalogue;
        for (int game = 0; game < GameCount; game++) {
            catalogue[kGameKeys[game]] = nlohmann::json::parse(sGames[game]->Request("DumpSettings"));
            catalogue[kGameKeys[game]]["definitions"] =
                nlohmann::json::parse(sGames[game]->Request("DumpSettingDefinitions"));
        }
        std::ofstream output(options.cataloguePath);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << catalogue.dump(2);
        return 0;
    }
    if (!options.playthroughPath.empty()) {
        return ValidateSpoiler(options);
    }
    if (!options.settingsPath.empty() && !RestoreSettingsFrom(options.settingsPath, options, commandLineGoal)) {
        return 2;
    }
    SettleTriforcePair(options);

    if (!options.poolPath.empty()) {
        return WritePool(options);
    }
    Fill fill = FillWithRetries(options);
    if (!fill.result.success) {
        sGames[Mm]->Request("Finish");
        std::cerr << "[fleet-seedgen] seed " << options.seed << " failed: " << fill.result.error << "\n";
        return 1;
    }
    // Finish drops the seed's restored lists, so the settings snapshot is taken first.
    const nlohmann::json spoiler = BuildSpoiler(fill, options);
    sGames[Mm]->Request("Finish");
    std::cout << "[fleet-seedgen] seed " << options.seed << " PASS: " << spoiler["oot"]["placements"].size()
              << " OoT and " << spoiler["mm"]["placements"].size() << " MM placements, " << spoiler["foreign"].size()
              << " across games\n";
    if (!options.outputPath.empty()) {
        std::ofstream(options.outputPath) << spoiler.dump(2);
    }
    return 0;
}

} // namespace

#ifdef FLEET_INTERNAL_WORKER
int FleetSeedWorker(int argc, char* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    int status = 1;
    try {
        status = GenerateSeed(argc, argv);
    } catch (const std::exception& e) { std::cerr << "[fleet-seedgen] generation aborted: " << e.what() << "\n"; }
    std::cout.flush();
    std::cerr.flush();
    // The games have no headless shutdown yet: a normal exit would run their static destructors after
    // their resource workers were killed mid-task, so the process ends without tearing anything down.
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(status));
#endif
    std::_Exit(status);
}
