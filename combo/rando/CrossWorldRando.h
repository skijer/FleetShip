#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <nlohmann/json.hpp>
#include "gui/ComboGenProgress.h"
#include "CrossForeign.h"
#include "SharedItems.h"
#include "CrossDungeonReachability.h"
#include "MagicMirrorShuffle.h"

namespace ComboRando {

struct CwRng {
    uint64_t s;
    explicit CwRng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {
    }
    uint32_t next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(s >> 33);
    }
    uint32_t below(uint32_t n) {
        return n ? next() % n : 0;
    }
};

inline constexpr const char* kMmJunkName = "Junk";

inline uint32_t CwHashName(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h = (h ^ c) * 16777619u;
    }
    return h;
}

template <class T> inline void cwShuffle(std::vector<T>& v, CwRng& rng) {
    for (size_t i = v.size(); i > 1; --i) {
        size_t j = rng.below(static_cast<uint32_t>(i));
        std::swap(v[i - 1], v[j]);
    }
}

struct OracleFns {
    void (*Reset)(void);
    void (*SetOwnedItems)(const char*);
    const char* (*GetReachableChecks)(void);
    void (*PlaceItem)(const char*, const char*);
    // OOT only. Valid only immediately after GetReachableChecks, whose owned set it describes.
    uint8_t (*GetPortalOpen)(void) = nullptr;
    void (*SetDungeonAccess)(const char*) = nullptr;
    const char* (*GetDungeonExits)(void) = nullptr;
    uint16_t (*GetBossDefeated)(void) = nullptr;
    int (*CanEnterArrival)(int32_t) = nullptr;
    CrossDungeon::BossSourceMap CrossBossSources = CrossDungeon::EmptyBossSources();
};

enum class OotAccess { ALL_REACHABLE, BEATABLE_ONLY, NO_LOGIC };

inline OotAccess OotAccessFromDump(const std::string& sohDumpJson) {
    try {
        auto a = nlohmann::json::parse(sohDumpJson).value("accessibility", nlohmann::json::object());
        if (a.value("noLogic", false))
            return OotAccess::NO_LOGIC;
        if (!a.value("allLocationsReachable", true))
            return OotAccess::BEATABLE_ONLY;
    } catch (...) {}
    return OotAccess::ALL_REACHABLE;
}

inline constexpr const char* kOotTriforcePiece = "Triforce Piece";
inline constexpr const char* kMmTriforcePiece = "Piece of the Triforce";

struct CwGoal {
    bool hunt = false;
    int required = 0;
    int total = -1;
    bool either = false;
    int pieces = 0;
};

inline int CwPoolPieces(const CwGoal& goal) {
    return goal.hunt ? goal.total : goal.pieces;
}

inline bool IsBossGoalMet(const CwGoal& goal, bool ganonReachable, bool majoraReachable) {
    return goal.either ? (ganonReachable || majoraReachable) : (ganonReachable && majoraReachable);
}

// OOT takes the odd piece; -1 passes through so each game keeps its own slider.
inline int CwOotPieces(int total) {
    return total < 0 ? -1 : total - total / 2;
}
inline int CwMmPieces(int total) {
    return total < 0 ? -1 : total / 2;
}
// OOT's pool cannot absorb its half much past this; 100/100 trips an itemPool <= locCount assert.
inline constexpr int kMaxComboTriforcePieces = 100;

inline bool CwIsTriforcePiece(GameId itemGame, const std::string& itemName) {
    return itemName == (itemGame == GAME_OOT ? kOotTriforcePiece : kMmTriforcePiece);
}

inline int CountPoolTriforcePieces(const std::string& sohDumpJson, const std::string& mmDumpJson) {
    auto countIn = [](const std::string& dump, GameId g) {
        int n = 0;
        try {
            auto d = nlohmann::json::parse(dump);
            for (const auto& it : d.value("pool", nlohmann::json::array()))
                n += CwIsTriforcePiece(g, it.value("name", std::string())) ? 1 : 0;
        } catch (...) {}
        return n;
    };
    const int oot = countIn(sohDumpJson, GAME_OOT);
    const int mm = countIn(mmDumpJson, GAME_MM);
    std::cout << "[ComboShip] Triforce Hunt: combined pool holds " << oot << " OOT + " << mm << " MM piece(s)\n";
    if (oot == 0 || mm == 0)
        std::cerr << "[ComboShip] Triforce Hunt: WARNING — one game contributes no pieces (slider at 0, or its "
                     "piece item was renamed)\n";
    return oot + mm;
}

using Game = GameId;

// Only JUNK is ever discardable, independently of `advancement`.
enum class CwCat { JUNK, LESSER, HEALTH, BOSS_KEY, SMALL_KEY, TOKEN, MAJOR, MASK, STRAY_FAIRY, UNKNOWN };

inline CwCat CwCatFromName(const std::string& s) {
    if (s == "junk")
        return CwCat::JUNK;
    if (s == "lesser")
        return CwCat::LESSER;
    if (s == "health")
        return CwCat::HEALTH;
    if (s == "bossKey")
        return CwCat::BOSS_KEY;
    if (s == "smallKey")
        return CwCat::SMALL_KEY;
    if (s == "token")
        return CwCat::TOKEN;
    if (s == "major")
        return CwCat::MAJOR;
    if (s == "mask")
        return CwCat::MASK;
    if (s == "strayFairy")
        return CwCat::STRAY_FAIRY;
    return CwCat::UNKNOWN;
}

inline const char* CwCatName(CwCat c) {
    switch (c) {
        case CwCat::JUNK:
            return "junk";
        case CwCat::LESSER:
            return "lesser";
        case CwCat::HEALTH:
            return "health";
        case CwCat::BOSS_KEY:
            return "bossKey";
        case CwCat::SMALL_KEY:
            return "smallKey";
        case CwCat::TOKEN:
            return "token";
        case CwCat::MAJOR:
            return "major";
        case CwCat::MASK:
            return "mask";
        case CwCat::STRAY_FAIRY:
            return "strayFairy";
        default:
            return "unknown";
    }
}

struct CwItem {
    Game game;
    std::string name;
    bool advancement;
    CwCat cat = CwCat::UNKNOWN;
    std::string group;
    // OOT files bombchus and Buy Blue Fire/Arrows as JUNK with advancement=true, so those never absorb a surplus.
    bool discardable() const {
        return cat == CwCat::JUNK && !advancement;
    }
};

struct CwCheck {
    Game game;
    std::string name;
};

struct CwPlacement {
    CwCheck check;
    CwItem item;
};

struct CombinedFillResult {
    std::string spoilerJson;
    bool success;
    std::string error;
    nlohmann::json pool;
};

inline nlohmann::json CombinedPoolJson(const std::vector<CwItem>& advItems, const std::vector<CwItem>& junkItems,
                                       const std::vector<CwPlacement>& fixed,
                                       const std::set<std::string>& sharedNames) {
    std::map<std::pair<Game, std::string>, nlohmann::json> entries;
    auto count = [&](const CwItem& item, const char* field) {
        auto& entry = entries[{ item.game, item.name }];
        if (entry.is_null()) {
            entry = { { "game", item.game == GAME_OOT ? "oot" : "mm" },
                      { "name", item.name },
                      { "category", CwCatName(item.cat) },
                      { "group", item.group },
                      { "advancement", item.advancement },
                      { "junk", item.discardable() },
                      { "shared", sharedNames.contains(item.name) },
                      { "shuffled", 0 },
                      { "fixed", 0 } };
        }
        entry[field] = entry[field].get<int>() + 1;
    };
    for (const auto* pool : { &advItems, &junkItems }) {
        for (const auto& item : *pool) {
            count(item, "shuffled");
        }
    }
    for (const auto& placement : fixed) {
        count(placement.item, "fixed");
    }
    nlohmann::json out = nlohmann::json::array();
    for (auto& [key, entry] : entries) {
        out.push_back(std::move(entry));
    }
    return out;
}

// Retry budgets are pass counts, never wall-clock, so success never depends on the machine.
// kFillAttempts must match the in-game and headless generators for their seeds to agree.
constexpr int kFillAttempts = 2;
constexpr int kMaxPasses = 3;
constexpr int kMaxPrereqTries = 4;

// MM access is gated on the OOT->MM portal region via ootOracle.GetPortalOpen. `progress` may be nullptr.
// Under GAME_MM start the portal prerequisites are still derived, as the re-entry guarantee.

// Most songs, and so most of both worlds, wait on the ocarina and its buttons. Placed late in the assumed
// fill they find every check still reachable already taken and the pass dead-ends; they are placed first.
inline bool IsPlacedFirst(const CwItem& item) {
    static constexpr SharedFamily kFirst[] = { SF_OCARINA,        SF_OCARINA_A,      SF_OCARINA_C_UP,
                                               SF_OCARINA_C_DOWN, SF_OCARINA_C_LEFT, SF_OCARINA_C_RIGHT };
    for (SharedFamily family : kFirst) {
        const auto& definition = SharedFamilyByIndex(family);
        if (item.name == (item.game == GAME_OOT ? definition.ootName : definition.mmName)) {
            return true;
        }
    }
    return false;
}

constexpr char kExtraBottlePrefix[] = "nei.bottle_";
constexpr size_t kExtendedBottleSlots = 8;

struct CwBottleSlots {
    size_t perGame[2] = { 4, 6 };
};

// The dumps do not mark bottles. Refills and the Bottomless Bottle fill no slot, so their names stay out.
inline bool IsBottleItem(const std::string& name) {
    return name == "Empty Bottle" || name.rfind("Bottle ", 0) == 0 || name.rfind(kExtraBottlePrefix, 0) == 0 ||
           name.rfind("fleet.bottle.", 0) == 0;
}

// The shared-group dedupe may leave a game's pool without an extra bottle, so the item catalogue decides.
inline bool ListsExtraBottles(const std::string& dumpJson) {
    const auto dump = nlohmann::json::parse(dumpJson, nullptr, false);

    if (!dump.is_object()) {
        return false;
    }
    const auto items = dump.value("items", nlohmann::json::array());
    return std::any_of(items.begin(), items.end(), [](const nlohmann::json& item) {
        return item.is_object() && item.value("name", "").rfind(kExtraBottlePrefix, 0) == 0;
    });
}

inline CwBottleSlots ReadBottleSlots(const std::string& ootDumpJson, const std::string& mmDumpJson) {
    CwBottleSlots slots;

    if (ListsExtraBottles(ootDumpJson)) {
        slots.perGame[GAME_OOT] = kExtendedBottleSlots;
    }
    if (ListsExtraBottles(mmDumpJson)) {
        slots.perGame[GAME_MM] = kExtendedBottleSlots;
    }
    return slots;
}

struct CwBottleKind {
    CwSharedName item;
    bool advancement = false;
    size_t group = 0;
    bool arrivesIn[2] = { false, false };
    size_t shuffled = 0;
    size_t fixed = 0;

    bool IsShared() const {
        return arrivesIn[GAME_OOT] && arrivesIn[GAME_MM];
    }
};

inline CwBottleKind& FindOrAddBottleKind(std::vector<CwBottleKind>& kinds, const CwItem& item,
                                         const std::vector<CwSharedGroup>& groups) {
    for (auto& kind : kinds) {
        if (kind.item.game == item.game && kind.item.name == item.name) {
            return kind;
        }
    }
    CwBottleKind kind{ { item.game, item.name }, item.advancement, groups.size() + kinds.size() };
    kind.arrivesIn[item.game] = true;
    for (size_t i = 0; i < groups.size(); ++i) {
        const auto& names = groups[i].names;
        const bool isMember = std::any_of(names.begin(), names.end(), [&](const CwSharedName& name) {
            return name.game == item.game && name.name == item.name;
        });
        if (!isMember) {
            continue;
        }
        kind.group = i;
        for (const auto& name : names) {
            kind.arrivesIn[name.game] = true;
        }
    }
    kinds.push_back(kind);
    return kinds.back();
}

inline size_t CountBottlesArriving(const std::vector<CwBottleKind>& kinds, Game game) {
    size_t count = 0;
    for (const auto& kind : kinds) {
        count += kind.arrivesIn[game] ? kind.shuffled + kind.fixed : 0;
    }
    return count;
}

inline size_t CountGroupBottles(const std::vector<CwBottleKind>& kinds, size_t group) {
    size_t count = 0;
    for (const auto& kind : kinds) {
        count += kind.group == group ? kind.shuffled + kind.fixed : 0;
    }
    return count;
}

// A copy both games receive goes first while the other game is over its slots too, and last once it is not.
inline bool IsBetterSurplus(const CwBottleKind& candidate, const CwBottleKind& best, bool isOtherGameOver) {
    const bool candidateFits = candidate.IsShared() == isOtherGameOver;
    const bool bestFits = best.IsShared() == isOtherGameOver;

    if (candidateFits != bestFits) {
        return candidateFits;
    }
    return candidate.shuffled > best.shuffled;
}

inline bool IsCriticalBottle(const CwSharedName& item) {
    const BottleTwin* twin = FindBottleTwin(item.game, item.name);
    return twin != nullptr && twin->isCritical;
}

// Progression keeps the last copy of its group; every other shuffled copy that `full` receives is spare. Shared,
// every bottle is spare but the quest ones, and empty bottles go first.
inline CwBottleKind* PickSurplusBottle(std::vector<CwBottleKind>& kinds, Game full, const CwBottleSlots& slots,
                                       bool sharedBottles) {
    const Game other = full == GAME_OOT ? GAME_MM : GAME_OOT;
    const bool isOtherGameOver = CountBottlesArriving(kinds, other) > slots.perGame[other];
    CwBottleKind* best = nullptr;

    for (auto& kind : kinds) {
        const bool isSpare = sharedBottles ? !IsCriticalBottle(kind.item)
                                           : !kind.advancement || CountGroupBottles(kinds, kind.group) > 1;

        if (!kind.arrivesIn[full] || kind.shuffled == 0 || !isSpare) {
            continue;
        }
        const bool isEmpty = kind.item.name == "Empty Bottle";
        const bool isBestEmpty = best != nullptr && best->item.name == "Empty Bottle";
        if (best == nullptr || (sharedBottles && isEmpty && !isBestEmpty) ||
            ((!sharedBottles || isEmpty == isBestEmpty) && IsBetterSurplus(kind, *best, isOtherGameOver))) {
            best = &kind;
        }
    }
    return best;
}

inline bool DropOneCopy(std::vector<CwItem>& pool, const CwSharedName& item) {
    const auto found = std::find_if(pool.begin(), pool.end(), [&](const CwItem& pooled) {
        return pooled.game == item.game && pooled.name == item.name;
    });

    if (found == pool.end()) {
        return false;
    }
    pool.erase(found);
    return true;
}

inline void CountBottleKinds(std::vector<CwBottleKind>& kinds, const std::vector<CwItem>& items,
                             const std::vector<CwSharedGroup>& groups, size_t CwBottleKind::*copies) {
    for (const auto& item : items) {
        if (IsBottleItem(item.name)) {
            ++(FindOrAddBottleKind(kinds, item, groups).*copies);
        }
    }
}

inline std::vector<CwBottleKind> CollectBottleKinds(const std::vector<CwItem>& advItems,
                                                    const std::vector<CwItem>& junkItems,
                                                    const std::vector<CwItem>& fixedItems,
                                                    const std::vector<CwSharedGroup>& groups) {
    std::vector<CwBottleKind> kinds;

    CountBottleKinds(kinds, advItems, groups, &CwBottleKind::shuffled);
    CountBottleKinds(kinds, junkItems, groups, &CwBottleKind::shuffled);
    CountBottleKinds(kinds, fixedItems, groups, &CwBottleKind::fixed);
    // A total order, so the pool's own order cannot decide which of two equal copies goes.
    std::sort(kinds.begin(), kinds.end(), [](const CwBottleKind& a, const CwBottleKind& b) {
        return std::tie(a.item.game, a.item.name) < std::tie(b.item.game, b.item.name);
    });
    return kinds;
}

// A bottle past a game's slots is a dead check. Only spare copies go, so a game stays over when every bottle
// left is the last of a progression item.
inline size_t CapBottles(std::vector<CwBottleKind> kinds, const CwBottleSlots& slots, std::vector<CwItem>& advItems,
                         std::vector<CwItem>& junkItems, bool sharedBottles) {
    size_t dropped = 0;

    for (Game game : { GAME_OOT, GAME_MM }) {
        while (CountBottlesArriving(kinds, game) > slots.perGame[game]) {
            CwBottleKind* surplus = PickSurplusBottle(kinds, game, slots, sharedBottles);

            if (surplus == nullptr) {
                break;
            }
            --surplus->shuffled;
            if (!DropOneCopy(advItems, surplus->item)) {
                DropOneCopy(junkItems, surplus->item);
            }
            ++dropped;
        }
    }
    std::cout << "[ComboShip] bottles: " << dropped << " surplus copies removed; oot receives "
              << CountBottlesArriving(kinds, GAME_OOT) << " of " << slots.perGame[GAME_OOT] << " slots, mm "
              << CountBottlesArriving(kinds, GAME_MM) << " of " << slots.perGame[GAME_MM] << "\n";
    return dropped;
}

struct CwZones {
    std::regex oot;
    std::regex mm;

    bool InZone(const CwCheck& check) const {
        return std::regex_search(check.name, check.game == GAME_OOT ? oot : mm);
    }
    bool Allows(const CwCheck& check, const CwItem& item) const {
        return InZone(check) == (item.game != check.game);
    }
};

enum class CwRewardPlacement { NATIVE, BOSSES_THIS_GAME, BOSSES_BOTH_GAMES };

inline CwRewardPlacement CwRewardPlacementFromName(const std::string& name) {
    if (name == "bossesThisGame") {
        return CwRewardPlacement::BOSSES_THIS_GAME;
    }
    if (name == "bossesBothGames") {
        return CwRewardPlacement::BOSSES_BOTH_GAMES;
    }
    return CwRewardPlacement::NATIVE;
}

enum class CwSongPlacement { NATIVE, SONG_LOCATIONS, DUNGEON_REWARDS, ANYWHERE };

inline CwSongPlacement CwSongPlacementFromName(const std::string& name) {
    if (name == "songLocations") {
        return CwSongPlacement::SONG_LOCATIONS;
    }
    if (name == "dungeonRewards") {
        return CwSongPlacement::DUNGEON_REWARDS;
    }
    if (name == "anywhere") {
        return CwSongPlacement::ANYWHERE;
    }
    return CwSongPlacement::NATIVE;
}

inline const char* CwSongCheckType(CwSongPlacement songs) {
    switch (songs) {
        case CwSongPlacement::SONG_LOCATIONS:
            return "song";
        case CwSongPlacement::DUNGEON_REWARDS:
            return "dungeonReward";
        case CwSongPlacement::NATIVE:
        case CwSongPlacement::ANYWHERE:
            return "";
        default:
            std::cerr << "[ComboShip] unknown song placement " << static_cast<int>(songs) << "\n";
            return "";
    }
}

enum class CwPlandoState { VANILLA, JUNK, ITEM };

struct CwPlandoEntry {
    CwCheck check;
    CwPlandoState state = CwPlandoState::ITEM;
    Game itemGame = GAME_OOT;
    std::string item;
};

struct CwPoolEdit {
    Game game = GAME_OOT;
    std::string item;
    int delta = 0;
};

struct CwFillRules {
    std::optional<CwZones> zones;
    CwSongPlacement songs = CwSongPlacement::NATIVE;
    CwRewardPlacement rewards = CwRewardPlacement::NATIVE;
    std::set<std::string> ownGame;
    std::vector<CwPlandoEntry> plando;
    std::vector<CwPoolEdit> poolEdits;
};

inline bool ParseCwGame(const nlohmann::json& value, Game& game) {
    if (value == "oot") {
        game = GAME_OOT;
        return true;
    }
    if (value == "mm") {
        game = GAME_MM;
        return true;
    }
    return false;
}

inline std::optional<CwPlandoEntry> ParseCwPlandoEntry(const nlohmann::json& row) {
    CwPlandoEntry entry;
    const std::string state = row.value("state", std::string{});
    entry.check.name = row.value("check", std::string{});
    if (!ParseCwGame(row.value("game", nlohmann::json()), entry.check.game) || entry.check.name.empty()) {
        return std::nullopt;
    }
    if (state == "vanilla") {
        entry.state = CwPlandoState::VANILLA;
        return entry;
    }
    if (state == "junk") {
        entry.state = CwPlandoState::JUNK;
        return entry;
    }
    entry.item = row.value("item", std::string{});
    if (state != "item" || entry.item.empty() ||
        !ParseCwGame(row.value("itemGame", nlohmann::json()), entry.itemGame)) {
        return std::nullopt;
    }
    return entry;
}

inline CwFillRules ParseCwFillRules(const nlohmann::json& fleet) {
    CwFillRules rules;
    if (!fleet.is_object()) {
        return rules;
    }
    rules.songs = CwSongPlacementFromName(fleet.value("songs", std::string("native")));
    rules.rewards = CwRewardPlacementFromName(fleet.value("rewards", std::string("native")));
    for (const nlohmann::json& category : fleet.value("ownGame", nlohmann::json::array())) {
        if (category.is_string()) {
            rules.ownGame.insert(category.get<std::string>());
        }
    }
    for (const nlohmann::json& row : fleet.value("plando", nlohmann::json::array())) {
        if (const std::optional<CwPlandoEntry> entry = ParseCwPlandoEntry(row)) {
            rules.plando.push_back(*entry);
        } else {
            std::cerr << "[ComboShip] skipping malformed plando row " << row.dump() << "\n";
        }
    }
    for (const nlohmann::json& row : fleet.value("poolEdits", nlohmann::json::array())) {
        CwPoolEdit edit{ GAME_OOT, row.value("item", std::string{}), row.value("delta", 0) };
        if (!ParseCwGame(row.value("game", nlohmann::json()), edit.game) || edit.item.empty() || edit.delta == 0) {
            std::cerr << "[ComboShip] skipping malformed pool edit " << row.dump() << "\n";
            continue;
        }
        rules.poolEdits.push_back(edit);
    }
    return rules;
}

inline bool IsDungeonRewardName(const std::string& name) {
    static const std::set<std::string> kRewards = {
        "Kokiri's Emerald", "Goron's Ruby",     "Zora's Sapphire",    "Forest Medallion", "Fire Medallion",
        "Water Medallion",  "Spirit Medallion", "Shadow Medallion",   "Light Medallion",  "Odolwa's Remains",
        "Goht's Remains",   "Gyorg's Remains",  "Twinmold's Remains",
    };
    return kRewards.contains(name);
}

inline bool IsDungeonRewardItem(const std::string& name) {
    return IsDungeonRewardName(name) || name.starts_with("mm.remains.") || name.starts_with("nei.medallion.") ||
           name.starts_with("nei.spiritual_stone.");
}

inline Game CwRewardHomeGame(const std::string& name) {
    return name.find("emains") != std::string::npos ? GAME_MM : GAME_OOT;
}

// Maps and compasses carry no category in either dump, only the "keys" pool group and their names.
inline std::string CwOwnGameCategory(const CwItem& item) {
    switch (item.cat) {
        case CwCat::SMALL_KEY:
            return item.name.starts_with("Gerudo Fortress") ? "gerudoKey" : "smallKey";
        case CwCat::BOSS_KEY:
            return "bossKey";
        case CwCat::STRAY_FAIRY:
            return "strayFairy";
        default:
            break;
    }
    if (CwIsTriforcePiece(item.game, item.name)) {
        return "triforce";
    }
    if (IsDungeonRewardName(item.name)) {
        return "dungeonReward";
    }
    const bool isMapOrCompass =
        item.name.find(" Map") != std::string::npos || item.name.find(" Compass") != std::string::npos;
    return item.group == "keys" && isMapOrCompass ? "mapCompass" : "";
}

struct CwPlacementPolicy {
    const CwZones* zones = nullptr;
    std::set<std::string> ownGame;
    std::string songCheckType;
    std::set<std::pair<Game, std::string>> confinedSongs;
    CwRewardPlacement rewards = CwRewardPlacement::NATIVE;
    std::set<std::pair<Game, std::string>> confinedRewards;
    std::map<std::pair<Game, std::string>, std::string> checkTypes;

    // A song or reward that finds no reachable slot of its kind left goes anywhere instead.
    bool ReleaseToAnywhere(const CwItem& item) {
        const std::pair<Game, std::string> key = { item.game, item.name };
        return confinedSongs.erase(key) > 0 || confinedRewards.erase(key) > 0;
    }

    bool IsOwnGame(const CwItem& item) const {
        return !ownGame.empty() && ownGame.contains(CwOwnGameCategory(item));
    }
    bool IsConfinedSong(const CwItem& item) const {
        return confinedSongs.contains({ item.game, item.name });
    }
    bool IsConfinedReward(const CwItem& item) const {
        return confinedRewards.contains({ item.game, item.name });
    }
    bool IsPlacedBeforeTheRest(const CwItem& item) const {
        return IsConfinedSong(item) || IsConfinedReward(item);
    }
    bool IsRestricted(const CwItem& item) const {
        return IsOwnGame(item) || IsPlacedBeforeTheRest(item);
    }
    bool HasCheckType(const CwCheck& check, const std::string& type) const {
        const auto found = checkTypes.find({ check.game, check.name });
        return found != checkTypes.end() && found->second == type;
    }
    // OoT dumps these two as dungeon rewards, yet they are native song locations too.
    static bool IsOotSongReward(const CwCheck& check) {
        return check.game == GAME_OOT && (check.name == "Sheik in Ice Cavern" || check.name == "Song from Impa");
    }
    bool IsSongSlot(const CwCheck& check) const {
        return HasCheckType(check, songCheckType) || (songCheckType == "song" && IsOotSongReward(check));
    }
    // OoT's "dungeonReward" checks are the heart containers; the rewards are the blue-warp checks.
    bool IsBossRewardSlot(const CwCheck& check) const {
        static const std::set<std::string> kOotBossRewards = { "Queen Gohma",   "King Dodongo", "Barinade",
                                                               "Phantom Ganon", "Volvagia",     "Morpha",
                                                               "Bongo Bongo",   "Twinrova" };
        if (check.game == GAME_OOT) {
            return kOotBossRewards.contains(check.name);
        }
        return HasCheckType(check, "dungeonReward");
    }
    bool Allows(const CwCheck& check, const CwItem& item) const {
        if (zones != nullptr && !zones->Allows(check, item)) {
            return false;
        }
        if (IsOwnGame(item) && check.game != item.game) {
            return false;
        }
        if (IsConfinedReward(item)) {
            return IsBossRewardSlot(check) &&
                   (rewards == CwRewardPlacement::BOSSES_BOTH_GAMES || check.game == CwRewardHomeGame(item.name));
        }
        return !IsConfinedSong(item) || IsSongSlot(check);
    }
};

inline std::optional<CwItem> TakeOneItem(std::vector<CwItem>& items, Game game, const std::string& name) {
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].game == game && items[i].name == name) {
            CwItem taken = std::move(items[i]);
            items[i] = items.back();
            items.pop_back();
            return taken;
        }
    }
    return std::nullopt;
}

inline void ConfineRewards(CwPlacementPolicy& policy, const std::vector<CwItem>& advItems) {
    for (const CwItem& item : advItems) {
        if (IsDungeonRewardItem(item.name)) {
            policy.confinedRewards.insert({ item.game, item.name });
        }
    }
    std::cout << "[ComboShip] dungeon rewards: " << policy.confinedRewards.size() << " confined to boss rewards\n";
}

// Every advancement song is confined; the fill releases a song only when no song check is left within its reach,
// so songs beyond the song checks, or behind checks only they open, still land.
inline void ConfineSongs(CwPlacementPolicy& policy, const std::set<std::pair<Game, std::string>>& songNames,
                         size_t slots, const std::vector<CwItem>& advItems) {
    for (const CwItem& item : advItems) {
        if (songNames.contains({ item.game, item.name })) {
            policy.confinedSongs.insert({ item.game, item.name });
        }
    }
    std::cout << "[ComboShip] songs: " << policy.confinedSongs.size() << " songs for " << slots << " song checks\n";
}

inline CombinedFillResult
CrossWorldCombinedFill(const std::string& sohDumpJson, const std::string& mmDumpJson, uint32_t masterSeed,
                       const OracleFns& ootOracle, const OracleFns& mmOracle,
                       ComboRando::ComboGenProgress* progress = nullptr, const std::string& forcedOotJson = "",
                       OotAccess ootAccess = OotAccess::ALL_REACHABLE, CwGoal goal = {}, GameId startingGame = GAME_OOT,
                       const std::string& sharedPairsJson = "", uint64_t sharedMask = 0, bool poolOnly = false,
                       bool plentifulModItems = false, const CwFillRules& rules = {}) {
    CombinedFillResult result;
    result.success = false;
    const CwZones* zones = rules.zones ? &*rules.zones : nullptr;
    CwPlacementPolicy policy{ .zones = zones,
                              .ownGame = rules.ownGame,
                              .songCheckType = CwSongCheckType(rules.songs),
                              .rewards = rules.rewards };
    std::set<std::pair<Game, std::string>> songNames;
    std::map<std::pair<Game, std::string>, std::string> vanillaItems;
    std::map<std::pair<Game, std::string>, bool> catalogueAdvancement;

    // NO_LOGIC deliberately skips the portal gate; any other mode without the export would fill MM as sphere-0
    // reachable.
    const bool crossDungeon = ootOracle.SetDungeonAccess && mmOracle.SetDungeonAccess && ootOracle.GetDungeonExits &&
                              mmOracle.GetDungeonExits;
    const bool portalGated = ootAccess != OotAccess::NO_LOGIC && !crossDungeon;
    const bool mmStart = startingGame == GAME_MM;
    const std::vector<std::string> mmEntryItems = MmEntryItems(mmDumpJson);
    if (portalGated && ootOracle.GetPortalOpen == nullptr) {
        result.error = "OOT oracle is missing Combo_SOH_Rando_GetPortalOpen — rebuild soh.dll (the OOT->MM "
                       "portal cannot be gated without it)";
        return result;
    }

    if (progress)
        progress->phase.store(1);

    std::vector<CwItem> advItems, junkItems;
    std::vector<std::string> mmJunkNames;
    std::vector<CwCheck> allChecks;
    std::vector<CwPlacement> lockedPlacements;

    // A pool without categories means a stale DLL, which would otherwise surface as "no discardable junk".
    std::string missingCategory;

    auto parsePool = [&](Game game, const std::string& dumpJson) {
        auto d = nlohmann::json::parse(dumpJson);

        for (auto& c : d.value("checks", nlohmann::json::array())) {
            std::string name = c.value("name", "");
            if (name.empty()) {
                continue;
            }
            allChecks.push_back({ game, name });
            policy.checkTypes[{ game, name }] = c.value("type", std::string{});
            const std::string vanilla = c.value("vanilla", std::string{});
            if (!vanilla.empty()) {
                vanillaItems[{ game, name }] = vanilla;
            }
        }
        for (const auto& item : d.value("items", nlohmann::json::array())) {
            const std::string name = item.value("name", std::string{});
            catalogueAdvancement[{ game, name }] = item.value("advancement", true);
            if (item.value("song", false)) {
                songNames.insert({ game, name });
            }
        }

        if (d.contains("pool")) {
            size_t parsed = 0, withCategory = 0;
            for (auto& it : d["pool"]) {
                std::string name = it.value("name", "");
                if (name.empty())
                    continue;
                bool adv = it.value("advancement", true);
                CwCat cat = CwCatFromName(it.value("category", std::string{}));
                ++parsed;
                withCategory += (cat != CwCat::UNKNOWN) ? 1 : 0;
                (adv ? advItems : junkItems).push_back({ game, name, adv, cat, it.value("group", std::string{}) });
            }
            if (parsed > 0 && withCategory == 0)
                missingCategory = (game == GAME_OOT ? "soh.dll" : "2ship.dll");
        } else {
            // Older DLL: one vanilla item per check, so the balancer no-ops.
            for (auto& c : d.value("checks", nlohmann::json::array())) {
                std::string vi = c.value("vanillaItem", "");
                if (vi.empty())
                    continue;
                bool adv = c.value("advancement", true);
                (adv ? advItems : junkItems).push_back({ game, vi, adv, adv ? CwCat::UNKNOWN : CwCat::JUNK });
            }
        }

        if (game == GAME_MM) {
            for (auto& j : d.value("junkPool", nlohmann::json::array())) {
                if (j.is_string() && !j.get<std::string>().empty())
                    mmJunkNames.push_back(j.get<std::string>());
            }
        }

        // Locked items never enter junkItems, so their category only labels the pool viewer.
        for (auto& f : d.value("fixed", nlohmann::json::array())) {
            std::string check = f.value("check", "");
            std::string item = f.value("item", "");
            if (check.empty() || item.empty())
                continue;
            lockedPlacements.push_back(
                { { game, check },
                  { game, item, f.value("advancement", true), CwCatFromName(f.value("category", std::string{})),
                    f.value("group", std::string{}) } });
        }
    };

    try {
        parsePool(GAME_OOT, sohDumpJson);
        parsePool(GAME_MM, mmDumpJson);
    } catch (const std::exception& e) {
        result.error = std::string("Pool parse error: ") + e.what();
        return result;
    }
    if (!missingCategory.empty()) {
        result.error = missingCategory +
                       " is stale: its pool[] has no per-item `category`, so the fill cannot tell junk from "
                       "progression and refuses to balance the pool — rebuild soh.dll / 2ship.dll";
        std::cerr << "[ComboShip] CrossWorldCombinedFill: " << result.error << "\n";
        return result;
    }
    // Under an MM start the Mask Shop Key is forced start-with, so a confined placement means the force never reached
    // OOT.
    if (mmStart) {
        bool lockedDoors = false;
        try {
            lockedDoors = nlohmann::json::parse(sohDumpJson)
                              .value("accessibility", nlohmann::json::object())
                              .value("lockOverworldDoors", false);
        } catch (...) {}
        const bool keyPlaced =
            lockedDoors && std::any_of(lockedPlacements.begin(), lockedPlacements.end(), [](const CwPlacement& p) {
                return p.item.game == GAME_OOT && p.item.name == "Mask Shop Key";
            });
        if (keyPlaced)
            std::cerr << "[ComboShip] CrossWorldCombinedFill: WARNING — MM start with Lock Overworld Doors, but the "
                         "Mask Shop Key was confined-placed (Exclude Mask Shop Key was not forced on)\n";
    }

    // Forced items are reserved out of the cross pool and owned from the start.
    std::vector<CwPlacement> forcedPlacements;
    std::vector<std::string> ootForcedOwned, mmForcedOwned;
    nlohmann::json forcedSpecs =
        forcedOotJson.empty() ? nlohmann::json::object() : nlohmann::json::parse(forcedOotJson, nullptr, false);
    if (!forcedSpecs.is_object()) {
        forcedSpecs = nlohmann::json::object();
    }
    auto ownFromStart = [&](const CwPlacement& placement) {
        forcedPlacements.push_back(placement);
        (placement.item.game == GAME_OOT ? ootForcedOwned : mmForcedOwned).push_back(placement.item.name);
    };

    CwRng plandoRng(masterSeed ^ 0x504C414Eu);
    auto takeRandomJunk = [&](Game game) -> std::optional<CwItem> {
        std::vector<size_t> junk;
        for (size_t i = 0; i < junkItems.size(); ++i) {
            if (junkItems[i].game == game && junkItems[i].discardable()) {
                junk.push_back(i);
            }
        }
        if (junk.empty()) {
            return std::nullopt;
        }
        const size_t pick = junk[plandoRng.below(static_cast<uint32_t>(junk.size()))];
        CwItem taken = junkItems[pick];
        junkItems[pick] = junkItems.back();
        junkItems.pop_back();
        return taken;
    };
    auto takePlandoItem = [&](const CwPlandoEntry& entry) -> std::optional<CwItem> {
        if (entry.state == CwPlandoState::JUNK) {
            return takeRandomJunk(entry.check.game);
        }
        const bool isVanilla = entry.state == CwPlandoState::VANILLA;
        const auto vanilla = vanillaItems.find({ entry.check.game, entry.check.name });
        const Game game = isVanilla ? entry.check.game : entry.itemGame;
        const std::string name = !isVanilla ? entry.item : vanilla != vanillaItems.end() ? vanilla->second : "";
        if (name.empty()) {
            return std::nullopt;
        }
        if (std::optional<CwItem> pooled = TakeOneItem(advItems, game, name)) {
            return pooled;
        }
        if (std::optional<CwItem> pooled = TakeOneItem(junkItems, game, name)) {
            return pooled;
        }
        const auto known = catalogueAdvancement.find({ game, name });
        return CwItem{ game, name, known == catalogueAdvancement.end() || known->second };
    };

    std::vector<CwPlacement> plandoPlacements;
    for (const CwPlandoEntry& entry : rules.plando) {
        const bool isForcedCheck = entry.check.game == GAME_OOT && forcedSpecs.contains(entry.check.name);
        const auto freeCheck = std::find_if(allChecks.begin(), allChecks.end(), [&](const CwCheck& check) {
            return check.game == entry.check.game && check.name == entry.check.name;
        });
        if (!isForcedCheck && freeCheck == allChecks.end()) {
            std::cerr << "[ComboShip] plando: '" << entry.check.name << "' is no free check under these settings\n";
            continue;
        }
        const std::optional<CwItem> item = takePlandoItem(entry);
        if (!item) {
            std::cerr << "[ComboShip] plando: no item to place at '" << entry.check.name << "'\n";
            continue;
        }
        if (isForcedCheck) {
            forcedSpecs.erase(entry.check.name);
            ownFromStart({ entry.check, *item });
            continue;
        }
        allChecks.erase(freeCheck);
        plandoPlacements.push_back({ entry.check, *item });
    }
    if (!plandoPlacements.empty()) {
        std::cout << "[ComboShip] plando: " << plandoPlacements.size() << " check(s) fixed\n";
    }

    CwRng frng(masterSeed ^ 0xF0F0F0F0u);
    try {
        for (auto& [checkName, spec] : forcedSpecs.items()) {
            CwItem forced{ GAME_OOT, {}, true };
            if (spec.contains("item")) {
                const std::string itemName = spec.value("item", std::string{});
                std::optional<CwItem> pooled = TakeOneItem(advItems, GAME_OOT, itemName);
                if (!pooled) {
                    pooled = TakeOneItem(junkItems, GAME_OOT, itemName);
                }
                forced = pooled.value_or(CwItem{ GAME_OOT, itemName, true,
                                                 CwCatFromName(spec.value("itemCategory", std::string{})),
                                                 spec.value("group", std::string{}) });
            } else {
                const std::string cat = spec.value("category", std::string("any"));
                std::vector<size_t> idx;
                std::vector<CwItem>* src = nullptr;
                for (size_t i = 0; i < advItems.size(); ++i) {
                    if (advItems[i].game == GAME_OOT) {
                        idx.push_back(i);
                    }
                }
                if (!idx.empty()) {
                    src = &advItems;
                } else if (cat == "any") {
                    for (size_t i = 0; i < junkItems.size(); ++i) {
                        if (junkItems[i].game == GAME_OOT) {
                            idx.push_back(i);
                        }
                    }
                    if (!idx.empty()) {
                        src = &junkItems;
                    }
                }
                if (src) {
                    const size_t pick = idx[frng.below(static_cast<uint32_t>(idx.size()))];
                    forced = std::move((*src)[pick]);
                    (*src)[pick] = src->back();
                    src->pop_back();
                }
            }
            if (forced.name.empty()) {
                continue;
            }
            ownFromStart({ { GAME_OOT, checkName }, forced });
        }
    } catch (...) {}

    // Effective native families are determined before any overlapping MM copy is removed.
    auto countNamed = [&](Game game, const std::string& name, bool includeLocked = true) {
        size_t count = 0;
        for (const auto* pool : { &advItems, &junkItems })
            for (const auto& item : *pool)
                count += item.game == game && item.name == name;
        for (const auto* fixed : { &lockedPlacements, &forcedPlacements, &plandoPlacements }) {
            if (fixed == &lockedPlacements && !includeLocked)
                continue;
            for (const auto& p : *fixed)
                count += p.item.game == game && p.item.name == name;
        }
        return count;
    };
    uint64_t effectiveSharedMask = 0;
    bool maskQuestShuffle = false;
    if (sharedMask != 0) {
        try {
            maskQuestShuffle = nlohmann::json::parse(sohDumpJson)
                                   .value("accessibility", nlohmann::json::object())
                                   .value("maskQuestShuffle", false);
        } catch (...) {}
    }
    for (int i = 0; i < SF_COUNT; ++i) {
        const auto& def = SharedFamilyByIndex(i);
        if (!(sharedMask & (uint64_t{ 1 } << i)) || (def.isMask && !maskQuestShuffle))
            continue;
        if (i != SF_BOTTLES && countNamed(GAME_OOT, def.ootName) == 0)
            continue;
        if (def.mmHasItem && countNamed(GAME_MM, def.mmName) == 0 && FindTierLadder(i) == nullptr)
            continue;
        effectiveSharedMask |= (uint64_t{ 1 } << i);
    }
    const auto sharedPairs = ResolveSharedPairs(sharedPairsJson, mmDumpJson, sohDumpJson);
    const auto sharedGroups = BuildSharedItemGroups(sharedPairs, effectiveSharedMask);
    size_t dedupedCopies = 0;
    for (const auto& group : sharedGroups) {
        const bool dynamic = std::any_of(sharedPairs.begin(), sharedPairs.end(), [&](const CwSharedPair& pair) {
            return !pair.key.empty() &&
                   std::any_of(group.names.begin(), group.names.end(), [&](const CwSharedName& name) {
                       return name.game == GAME_OOT && name.name == pair.ootName;
                   });
        });
        if (plentifulModItems && dynamic && !group.nativeFamily) {
            continue;
        }
        size_t target = 0, fixedCopies = 0;
        std::array<size_t, 2> tierCopies = {};
        for (const auto& n : group.names) {
            const size_t copies = countNamed(n.game, n.name, group.nativeFamily);
            if (IsTierAliasName(n.game, n.name)) {
                tierCopies[n.game] += copies;
                target = std::max(target, tierCopies[n.game]);
            }
            if (group.nei || n.game == GAME_OOT) {
                target = std::max(target, copies);
            } else if (group.countsMmTiers) {
                target = std::max(target, std::min(copies, group.mmTierCap));
            }
            // A copy fixed in one game already is the shared item; the other game's shuffled copy would duplicate it.
            for (const auto* fixed : { &lockedPlacements, &forcedPlacements, &plandoPlacements }) {
                for (const auto& p : *fixed)
                    fixedCopies += p.item.game == n.game && p.item.name == n.name;
            }
        }
        const auto isInGroup = [&](const CwItem& item) {
            return std::any_of(group.names.begin(), group.names.end(),
                               [&](const CwSharedName& n) { return item.game == n.game && item.name == n.name; });
        };
        const bool isGroupProgression = std::any_of(advItems.begin(), advItems.end(), isInGroup);
        const bool isTriforceGroup = std::any_of(group.names.begin(), group.names.end(), [](const CwSharedName& n) {
            return CwIsTriforcePiece(n.game, n.name);
        });
        size_t remaining = target > fixedCopies ? target - fixedCopies : 0;
        if (isTriforceGroup) {
            remaining = std::numeric_limits<size_t>::max();
        }
        for (const auto& n : group.names) {
            for (auto* pool : { &advItems, &junkItems }) {
                pool->erase(std::remove_if(pool->begin(), pool->end(),
                                           [&](const CwItem& item) {
                                               if (item.game != n.game || item.name != n.name)
                                                   return false;
                                               if (remaining > 0) {
                                                   --remaining;
                                                   return false;
                                               }
                                               ++dedupedCopies;
                                               return true;
                                           }),
                            pool->end());
            }
        }
        if (isGroupProgression) {
            const auto promoted = std::stable_partition(junkItems.begin(), junkItems.end(),
                                                        [&](const CwItem& item) { return !isInGroup(item); });
            for (auto item = promoted; item != junkItems.end(); ++item) {
                item->advancement = true;
                advItems.push_back(*item);
            }
            junkItems.erase(promoted, junkItems.end());
        }
    }
    for (const SameItemAlias& same : kSameItemAliases) {
        const CwSharedName alias = { same.game, same.alias };
        const bool isGrouped = std::any_of(sharedGroups.begin(), sharedGroups.end(), [&](const CwSharedGroup& group) {
            return std::any_of(group.names.begin(), group.names.end(),
                               [&](const CwSharedName& n) { return n.game == alias.game && n.name == alias.name; });
        });
        if (isGrouped || countNamed(same.game, same.anchor) == 0) {
            continue;
        }
        while (DropOneCopy(advItems, alias) || DropOneCopy(junkItems, alias)) {
            ++dedupedCopies;
        }
    }
    std::cout << "[ComboShip] shared items: " << sharedGroups.size() << " groups, " << dedupedCopies
              << " duplicate pool copies removed\n";
    std::vector<CwItem> fixedItems;
    for (const auto* fixed : { &lockedPlacements, &forcedPlacements, &plandoPlacements }) {
        for (const auto& placement : *fixed) {
            fixedItems.push_back(placement.item);
        }
    }
    CapBottles(CollectBottleKinds(advItems, junkItems, fixedItems, sharedGroups),
               ReadBottleSlots(sohDumpJson, mmDumpJson), advItems, junkItems,
               (effectiveSharedMask & (uint64_t{ 1 } << SF_BOTTLES)) != 0);

    for (const CwPoolEdit& edit : rules.poolEdits) {
        for (int copy = 0; copy < -edit.delta; ++copy) {
            if (!TakeOneItem(advItems, edit.game, edit.item) && !TakeOneItem(junkItems, edit.game, edit.item)) {
                break;
            }
        }
        if (edit.delta <= 0) {
            continue;
        }
        const auto sameItem = [&](const CwItem& item) { return item.game == edit.game && item.name == edit.item; };
        const auto pooledAdv = std::find_if(advItems.begin(), advItems.end(), sameItem);
        const auto pooledJunk = std::find_if(junkItems.begin(), junkItems.end(), sameItem);
        const auto known = catalogueAdvancement.find({ edit.game, edit.item });
        CwItem added{ edit.game, edit.item, known == catalogueAdvancement.end() || known->second };
        if (pooledAdv != advItems.end()) {
            added = *pooledAdv;
        } else if (pooledJunk != junkItems.end()) {
            added = *pooledJunk;
        }
        auto& pool = added.advancement ? advItems : junkItems;
        pool.insert(pool.end(), static_cast<size_t>(edit.delta), added);
    }

    if (!policy.songCheckType.empty()) {
        const size_t songSlots = static_cast<size_t>(std::count_if(
            allChecks.begin(), allChecks.end(), [&](const CwCheck& check) { return policy.IsSongSlot(check); }));
        ConfineSongs(policy, songNames, songSlots, advItems);
    }
    if (policy.rewards != CwRewardPlacement::NATIVE) {
        ConfineRewards(policy, advItems);
    }
    const CwPlacementPolicy initialPolicy = policy;

    // Both generators over-supply on purpose, so only junk may be sacrificed. RNG-free, so it cannot shift the stream.
    size_t trimmedTotal = 0, paddedTotal = 0;
    {
        auto countFor = [](const std::vector<CwItem>& v, Game g) {
            return static_cast<size_t>(std::count_if(v.begin(), v.end(), [g](const CwItem& i) { return i.game == g; }));
        };
        std::string balanceLine;
        std::vector<std::string> trimNotes;
        for (Game g : { GAME_OOT, GAME_MM }) {
            const char* tag = (g == GAME_OOT ? "oot" : "mm");
            const size_t checkCount =
                static_cast<size_t>(std::count_if(allChecks.begin(), allChecks.end(), [g, zones](const CwCheck& c) {
                    const bool inZone = zones != nullptr && zones->InZone(c);
                    return (c.game == g) != inZone;
                }));
            const size_t itemCount = countFor(advItems, g) + countFor(junkItems, g);
            size_t trimmed = 0, padded = 0;

            if (itemCount > checkCount) {
                // Total order (count desc, name asc), so hash iteration order cannot leak in.
                const size_t surplus = itemCount - checkCount;
                std::unordered_map<std::string, int> avail;
                for (const auto& i : junkItems)
                    if (i.game == g && i.discardable())
                        ++avail[i.name];
                std::unordered_map<std::string, int> remove;
                for (size_t k = 0; k < surplus; ++k) {
                    const std::string* best = nullptr;
                    int bestCount = 0;
                    for (const auto& [n, c] : avail) {
                        if (c <= 0)
                            continue;
                        if (!best || c > bestCount || (c == bestCount && n < *best)) {
                            best = &n;
                            bestCount = c;
                        }
                    }
                    if (!best) {
                        result.error = std::string("cannot balance the ") + tag +
                                       " pool: " + std::to_string(surplus - k) +
                                       " surplus item(s) left and no discardable junk remains; only junk may be "
                                       "discarded, so refusing to drop anything else";
                        std::cerr << "[ComboShip] CrossWorldCombinedFill: " << result.error << "\n";
                        return result;
                    }
                    --avail[*best];
                    ++remove[*best];
                }
                for (const auto& [n, c] : remove)
                    trimNotes.push_back(std::string(tag) + " junk: " + n + " x" + std::to_string(c));
                std::vector<CwItem> kept;
                kept.reserve(junkItems.size());
                for (auto& i : junkItems) {
                    auto it = (i.game == g && i.discardable()) ? remove.find(i.name) : remove.end();
                    if (it != remove.end() && it->second > 0) {
                        --it->second;
                        ++trimmed;
                        continue;
                    }
                    kept.push_back(std::move(i));
                }
                junkItems = std::move(kept);
            } else if (itemCount < checkCount) {
                // Never clone the other game's junk: its name would not resolve in this game's apply path.
                const size_t deficit = checkCount - itemCount;
                const CwItem* src = nullptr;
                for (const auto& i : junkItems)
                    if (i.game == g && i.discardable()) {
                        src = &i;
                        break;
                    }
                if (!src) {
                    result.error = std::string("cannot balance the ") + tag + " pool: " + std::to_string(deficit) +
                                   " check(s) short and no " + tag + " junk to clone";
                    std::cerr << "[ComboShip] CrossWorldCombinedFill: " << result.error << "\n";
                    return result;
                }
                CwItem filler = *src;
                for (size_t k = 0; k < deficit; ++k)
                    junkItems.push_back(filler);
                padded = deficit;
                std::cout << "[ComboShip] CrossWorldCombinedFill: " << tag << " pool was " << deficit
                          << " item(s) short of its checks; padded with junk ('" << filler.name
                          << "') — benign if a forced placement reserved an item, else the dump under-filled\n";
            }

            trimmedTotal += trimmed;
            paddedTotal += padded;
            balanceLine += std::string(balanceLine.empty() ? "" : ", ") + tag +
                           " items=" + std::to_string(countFor(advItems, g) + countFor(junkItems, g)) +
                           "/checks=" + std::to_string(checkCount) + " (trimmed " + std::to_string(trimmed) +
                           ", padded " + std::to_string(padded) + ")";
        }
        std::cout << "[ComboShip] CrossWorldCombinedFill: pool balance — " << balanceLine << " (forced "
                  << forcedPlacements.size() << ")\n";
        for (const auto& n : trimNotes)
            std::cout << "[ComboShip] CrossWorldCombinedFill:     trimmed " << n << "\n";
    }

    if (poolOnly) {
        std::vector<CwPlacement> fixed = lockedPlacements;
        fixed.insert(fixed.end(), forcedPlacements.begin(), forcedPlacements.end());
        fixed.insert(fixed.end(), plandoPlacements.begin(), plandoPlacements.end());
        result.pool =
            CombinedPoolJson(advItems, junkItems, fixed, SharedUntaggedNames(effectiveSharedMask, sharedPairs));
        result.success = true;
        return result;
    }

    CwRng rng(masterSeed);
    cwShuffle(allChecks, rng);

    auto checkKey = [](const CwCheck& c) { return std::string(c.game == GAME_OOT ? "oot:" : "mm:") + c.name; };

    // Locked checks are never re-validated: the oracles under-model some of them and would falsely fail.
    std::unordered_set<std::string> lockedCheckKeys;
    for (const auto& lp : lockedPlacements)
        lockedCheckKeys.insert(checkKey(lp.check));
    std::vector<CwPlacement> presetPlacements = lockedPlacements;
    presetPlacements.insert(presetPlacements.end(), plandoPlacements.begin(), plandoPlacements.end());
    std::unordered_set<std::string> presetCheckKeys = lockedCheckKeys;
    for (const auto& placement : plandoPlacements) {
        presetCheckKeys.insert(checkKey(placement.check));
    }

    struct QueryStats {
        uint32_t count = 0;
        int64_t ms = 0;
    };
    QueryStats ootStats, mmStats;

    auto queryReachable = [&](const OracleFns& oracle,
                              const std::vector<std::string>& ownedItems) -> std::unordered_set<std::string> {
        QueryStats& stats = (&oracle == &ootOracle) ? ootStats : mmStats;
        auto t0 = std::chrono::steady_clock::now();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& n : ownedItems)
            arr.push_back(n);
        oracle.Reset();
        oracle.SetOwnedItems(arr.dump().c_str());
        std::string raw = oracle.GetReachableChecks();
        std::unordered_set<std::string> out;
        try {
            auto parsed = nlohmann::json::parse(raw);
            for (const auto& name : parsed)
                out.insert(name.get<std::string>());
        } catch (...) {}
        stats.count++;
        stats.ms +=
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        return out;
    };

    // OOT-only with the portal shut; follows reachableFixpoint's rule so it cannot certify what that rejects.
    struct OotClosedResult {
        std::unordered_set<std::string> reach;
        bool portalOpen = false;
    };
    auto ootClosedFixpoint = [&](const std::vector<CwPlacement>& pl,
                                 const std::vector<std::string>& extraOwned) -> OotClosedResult {
        std::vector<std::string> owned = ootForcedOwned;
        owned.insert(owned.end(), extraOwned.begin(), extraOwned.end());
        std::vector<bool> credited(pl.size(), false);
        OotClosedResult r;
        for (;;) {
            r.reach = queryReachable(ootOracle, owned);
            r.portalOpen = r.portalOpen || ootOracle.GetPortalOpen == nullptr || ootOracle.GetPortalOpen() != 0;
            bool changed = false;
            for (size_t i = 0; i < pl.size(); ++i) {
                if (credited[i] || pl[i].check.game != GAME_OOT || pl[i].item.game != GAME_OOT)
                    continue;
                if (r.reach.count(pl[i].check.name)) {
                    owned.push_back(pl[i].item.name);
                    credited[i] = true;
                    changed = true;
                }
            }
            if (!changed)
                return r;
        }
    };

    // RNG-free (canonical order) so it cannot shift the seeded stream.
    std::vector<std::string> portalPrereqs;
    if (portalGated) {
        const int kMaxDeriveQueries = 1500;
        const uint32_t queries0 = ootStats.count;
        int used = 0;
        auto portalOpenWith = [&](const std::vector<std::string>& owned) {
            bool open = ootClosedFixpoint(presetPlacements, owned).portalOpen;
            used = static_cast<int>(ootStats.count - queries0);
            return open;
        };
        const std::vector<std::string> base;
        std::vector<std::string> rest;
        for (const auto& it : advItems)
            if (it.game == GAME_OOT)
                rest.push_back(it.name);
        std::sort(rest.begin(), rest.end());

        std::vector<std::string> witness;
        auto ownedWith = [&](size_t k) {
            std::vector<std::string> o = base;
            o.insert(o.end(), witness.begin(), witness.end());
            o.insert(o.end(), rest.begin(), rest.begin() + k);
            return o;
        };
        if (portalOpenWith(base)) {
            std::cout << "[ComboShip] CrossWorldCombinedFill: portal open from the start — no prerequisites\n";
        } else if (!portalOpenWith(ownedWith(rest.size()))) {
            std::cerr << "[ComboShip] CrossWorldCombinedFill: WARNING — the OOT->MM portal looks unreachable even "
                         "with every OOT item owned; generating anyway, MM may end up unreachable\n";
        } else {
            while (used < kMaxDeriveQueries && !rest.empty()) {
                size_t lo = 0, hi = rest.size();
                while (hi - lo > 1 && used < kMaxDeriveQueries) {
                    size_t mid = lo + (hi - lo) / 2;
                    if (portalOpenWith(ownedWith(mid)))
                        hi = mid;
                    else
                        lo = mid;
                }
                witness.push_back(rest[hi - 1]);
                rest.resize(hi - 1);
                if (portalOpenWith(ownedWith(0)))
                    break;
            }
            if (used >= kMaxDeriveQueries) {
                std::cerr << "[ComboShip] CrossWorldCombinedFill: WARNING — portal prerequisite derivation exceeded "
                             "its "
                          << kMaxDeriveQueries << "-query budget; falling back to an unconstrained fill\n";
            } else {
                portalPrereqs = witness;
                std::cout << "[ComboShip] CrossWorldCombinedFill: portal prerequisites (" << portalPrereqs.size()
                          << ", " << used << " queries):";
                for (const auto& n : portalPrereqs)
                    std::cout << " " << n;
                std::cout << "\n";
            }
        }
    }

    // Spans both games because an item at a foreign check can open progress anywhere, including the portal itself.
    std::vector<CwPlacement> placements;
    struct FixResult {
        std::unordered_set<std::string> ootReachable, mmReachable;
        std::vector<std::string> ootOwned;
    };
    auto reachableFixpoint = [&](const std::vector<std::string>& ootBase,
                                 const std::vector<std::string>& mmBase) -> FixResult {
        std::vector<std::string> ootOwned = ootBase, mmOwned = mmBase;
        ootOwned.insert(ootOwned.end(), ootForcedOwned.begin(), ootForcedOwned.end());
        mmOwned.insert(mmOwned.end(), mmForcedOwned.begin(), mmForcedOwned.end());
        std::vector<bool> credited(placements.size(), false);
        std::unordered_set<std::string> ootReachable, mmReachable;
        // Latched: once OOT can reach the portal it stays open.
        bool portalOpen = !portalGated || mmStart;
        std::vector<std::string> ootSeen, mmSeen;
        for (;;) {
            SharedOwnedViews(sharedGroups, ootOwned, mmOwned, ootSeen, mmSeen);
            if (crossDungeon) {
                const bool isMmOpenFromStart = mmStart || ootAccess == OotAccess::NO_LOGIC;
                const MmOverworld mmOverworld = ResolveMmOverworld(isMmOpenFromStart, mmEntryItems, mmSeen);
                auto joint =
                    CrossDungeon::QueryJoint(ootOracle, mmOracle, ootSeen, mmSeen, mmOverworld, queryReachable);
                ootReachable = std::move(joint.oot);
                mmReachable = std::move(joint.mm);
            } else {
                ootReachable = queryReachable(ootOracle, ootSeen);
                // Read before any MM check is credited, so the portal is never proven by an item behind it.
                if (!portalOpen) {
                    portalOpen = ootOracle.GetPortalOpen() != 0 && OwnsMmEntryItems(mmEntryItems, mmSeen);
                }
                mmReachable = portalOpen ? queryReachable(mmOracle, mmSeen) : std::unordered_set<std::string>{};
            }
            // The forest branch opens only its stump, independently of the main portal.
            MagicMirrorShuffle::AddOotRoute(ootReachable, mmReachable);
            bool changed = false;
            for (size_t i = 0; i < placements.size(); ++i) {
                if (credited[i])
                    continue;
                const auto& reach = placements[i].check.game == GAME_OOT ? ootReachable : mmReachable;
                if (reach.count(placements[i].check.name)) {
                    auto& owned = placements[i].item.game == GAME_OOT ? ootOwned : mmOwned;
                    owned.push_back(placements[i].item.name);
                    credited[i] = true;
                    changed = true;
                }
            }
            if (!changed)
                return { std::move(ootReachable), std::move(mmReachable), std::move(ootOwned) };
        }
    };

    if (progress) {
        progress->phase.store(2);
        progress->total.store(static_cast<int>(advItems.size()));
        progress->placed.store(0);
    }

    std::unordered_set<std::string> filledChecks;
    bool fillOk = false;
    int passesUsed = 0;
    std::string lastPassError;

    std::unordered_map<std::string, int> expectedItems;
    struct ExpectedInfo {
        CwCat cat;
        bool discardable;
    };
    std::unordered_map<std::string, ExpectedInfo> expectedInfo;
    for (const auto* src : { &advItems, &junkItems })
        for (const auto& i : *src) {
            const std::string key = std::string(i.game == GAME_OOT ? "oot:" : "mm:") + i.name;
            ++expectedItems[key];
            expectedInfo[key] = { i.cat, i.discardable() };
        }

    for (int pass = 1; pass <= kMaxPasses && !fillOk; ++pass) {
        if (GenCancelled(progress)) {
            result.error = "cancelled";
            return result;
        }
        passesUsed = pass;
        policy = initialPolicy;
        auto passStart = std::chrono::steady_clock::now();
        auto passMs = [&] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - passStart)
                .count();
        };
        placements.clear();
        filledChecks.clear();

        placements = presetPlacements;
        for (const auto& lp : presetPlacements)
            filledChecks.insert(checkKey(lp.check));

        // Portal prerequisites go first, into portal-closed-reachable checks chosen at random.
        std::vector<CwItem> advRest = advItems;
        bool prereqOk = portalPrereqs.empty();
        for (int t = 1; t <= kMaxPrereqTries && !prereqOk; ++t) {
            if (GenCancelled(progress)) {
                result.error = "cancelled";
                return result;
            }
            placements = presetPlacements;
            filledChecks.clear();
            for (const auto& lp : presetPlacements)
                filledChecks.insert(checkKey(lp.check));
            advRest = advItems;
            std::vector<CwItem> prereq;
            for (const auto& name : portalPrereqs) {
                for (size_t i = 0; i < advRest.size(); ++i) {
                    if (advRest[i].game == GAME_OOT && advRest[i].name == name) {
                        prereq.push_back(advRest[i]);
                        advRest.erase(advRest.begin() + i);
                        break;
                    }
                }
            }
            cwShuffle(prereq, rng);
            bool placedAll = true;
            for (const auto& item : prereq) {
                auto fr = ootClosedFixpoint(placements, {});
                std::vector<size_t> cands;
                for (size_t ci = 0; ci < allChecks.size(); ++ci) {
                    if (allChecks[ci].game != GAME_OOT || filledChecks.count(checkKey(allChecks[ci])))
                        continue;
                    if (!policy.Allows(allChecks[ci], item))
                        continue;
                    if (fr.reach.count(allChecks[ci].name))
                        cands.push_back(ci);
                }
                if (cands.empty()) {
                    placedAll = false;
                    break;
                }
                size_t pick = cands[rng.below(static_cast<uint32_t>(cands.size()))];
                placements.push_back({ allChecks[pick], item });
                filledChecks.insert(checkKey(allChecks[pick]));
            }
            prereqOk = placedAll && ootClosedFixpoint(placements, {}).portalOpen;
            if (!prereqOk)
                std::cerr << "[ComboShip] CrossWorldCombinedFill: portal still closed after prerequisite placement "
                             "(pass "
                          << pass << ", try " << t << "/" << kMaxPrereqTries << ") — repicking\n";
        }
        if (!prereqOk) {
            lastPassError = "the OOT->MM portal's prerequisites could not be placed within reach of the start";
            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << lastPassError << " (pass " << pass << ", "
                      << passMs() << " ms) — retrying\n";
            continue;
        }

        // Under NO_LOGIC only MM advancement is assumed-filled; OOT advancement rides the junk fast-fill.
        std::vector<CwItem> toPlace;
        if (ootAccess == OotAccess::NO_LOGIC) {
            for (const auto& it : advRest)
                if (it.game == GAME_MM)
                    toPlace.push_back(it);
        } else {
            toPlace = advRest;
        }
        std::vector<CwItem> relaxedToJunk;
        cwShuffle(toPlace, rng);
        // Items are taken from the back; RNG-free, so the shuffle still decides everything else.
        const auto placementRank = [&policy](const CwItem& item) {
            if (policy.IsPlacedBeforeTheRest(item)) {
                return 3;
            }
            if (IsPlacedFirst(item)) {
                return 2;
            }
            return policy.IsOwnGame(item) ? 1 : 0;
        };
        std::stable_sort(toPlace.begin(), toPlace.end(),
                         [&](const CwItem& a, const CwItem& b) { return placementRank(a) < placementRank(b); });
        std::vector<std::string> ootRemaining, mmRemaining;
        for (const auto& it : toPlace)
            (it.game == GAME_OOT ? ootRemaining : mmRemaining).push_back(it.name);

        // K items are placed off one fixpoint, each on a check reachable without itself or its batch.
        // The cap halves on a failed batch and only a failed K=1 ends the pass.
        constexpr size_t kBatchCap = 16;
        size_t batchCap = kBatchCap;
        bool isPlacingSongs = !policy.confinedSongs.empty() || !policy.confinedRewards.empty();
        auto removeOne = [](std::vector<std::string>& v, const std::string& name) {
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] == name) {
                    v[i] = v.back();
                    v.pop_back();
                    return;
                }
            }
        };

        bool deadEnd = false;
        while (!toPlace.empty()) {
            if (GenCancelled(progress)) {
                result.error = "cancelled";
                return result;
            }
            const CwItem& next = toPlace.back();
            if (isPlacingSongs && !songNames.contains({ next.game, next.name }) &&
                !(policy.rewards != CwRewardPlacement::NATIVE && IsDungeonRewardItem(next.name))) {
                isPlacingSongs = false;
                batchCap = kBatchCap;
            }
            size_t k = std::min({ batchCap, std::max<size_t>(1, toPlace.size() / 4), toPlace.size() });

            std::vector<CwItem> batch;
            batch.reserve(k);
            for (size_t i = 0; i < k; ++i) {
                batch.push_back(toPlace.back());
                toPlace.pop_back();
                removeOne(batch.back().game == GAME_OOT ? ootRemaining : mmRemaining, batch.back().name);
            }

            auto fr = reachableFixpoint(ootRemaining, mmRemaining);
            auto& ootReachable = fr.ootReachable;
            auto& mmReachable = fr.mmReachable;

            std::vector<size_t> candidates;
            for (size_t ci = 0; ci < allChecks.size(); ++ci) {
                if (filledChecks.count(checkKey(allChecks[ci])))
                    continue;
                const auto& reach = allChecks[ci].game == GAME_OOT ? ootReachable : mmReachable;
                if (reach.count(allChecks[ci].name))
                    candidates.push_back(ci);
            }

            std::vector<size_t> picks;
            if (candidates.size() >= batch.size()) {
                for (const auto& item : batch) {
                    std::vector<size_t> allowed;
                    for (size_t slot = 0; slot < candidates.size(); ++slot) {
                        if (policy.Allows(allChecks[candidates[slot]], item)) {
                            allowed.push_back(slot);
                        }
                    }
                    if (allowed.empty()) {
                        break;
                    }
                    if (zones != nullptr && (item.cat == CwCat::MAJOR || item.cat == CwCat::MASK)) {
                        std::vector<size_t> zoned;
                        for (size_t slot : allowed) {
                            if (allChecks[candidates[slot]].game != item.game) {
                                zoned.push_back(slot);
                            }
                        }
                        if (!zoned.empty()) {
                            allowed = std::move(zoned);
                        }
                    }
                    const size_t slot = allowed[rng.below(static_cast<uint32_t>(allowed.size()))];
                    picks.push_back(candidates[slot]);
                    candidates[slot] = candidates.back();
                    candidates.pop_back();
                }
            }

            if (picks.size() < batch.size()) {
                if (batch.size() == 1 && policy.ReleaseToAnywhere(batch.front())) {
                    (batch.front().game == GAME_OOT ? ootRemaining : mmRemaining).push_back(batch.front().name);
                    toPlace.push_back(batch.front());
                    continue;
                }
                // BEATABLE_ONLY lets a single stranded OOT advancement item fall to the junk fast-fill.
                if (batch.size() == 1 && ootAccess == OotAccess::BEATABLE_ONLY && batch.front().game == GAME_OOT) {
                    relaxedToJunk.push_back(batch.front());
                    continue;
                }
                for (auto it = batch.rbegin(); it != batch.rend(); ++it) {
                    (it->game == GAME_OOT ? ootRemaining : mmRemaining).push_back(it->name);
                    toPlace.push_back(*it);
                }
                if (batch.size() > 1) {
                    batchCap = batch.size() / 2;
                    continue;
                }
                lastPassError = "dead end placing '" + batch.front().name + "' (no reachable check left for it)";
                std::cerr << "[ComboShip] CrossWorldCombinedFill: dead end placing '" << batch.front().name
                          << "' (pass " << pass << ", " << placements.size() << " placed, " << passMs()
                          << " ms; reachable without it: oot " << ootReachable.size() << ", mm " << mmReachable.size()
                          << ", all filled) — retrying\n";
                if (ootReachable.size() + mmReachable.size() <= 8) {
                    for (const auto* reach : { &ootReachable, &mmReachable })
                        for (const auto& check : *reach)
                            std::cerr << "[ComboShip]     reachable: " << check << "\n";
                }
                deadEnd = true;
                break;
            }

            for (size_t i = 0; i < batch.size(); ++i) {
                placements.push_back({ allChecks[picks[i]], batch[i] });
                filledChecks.insert(checkKey(allChecks[picks[i]]));
            }
            if (progress)
                progress->placed.store(static_cast<int>(placements.size() - presetPlacements.size()));
        }
        if (deadEnd)
            continue;

        // Fast-fill makes no oracle calls; relaxed OOT advancement placed here is tolerated if unreachable.
        std::vector<CwItem> fastFillItems = junkItems;
        if (ootAccess == OotAccess::NO_LOGIC) {
            for (const auto& it : advRest)
                if (it.game == GAME_OOT)
                    fastFillItems.push_back(it);
        }
        fastFillItems.insert(fastFillItems.end(), relaxedToJunk.begin(), relaxedToJunk.end());
        cwShuffle(fastFillItems, rng);
        if (std::any_of(fastFillItems.begin(), fastFillItems.end(),
                        [&](const CwItem& item) { return policy.IsRestricted(item); })) {
            std::vector<CwItem> unrestricted;
            for (const CwItem& item : fastFillItems) {
                if (!policy.IsRestricted(item)) {
                    unrestricted.push_back(item);
                    continue;
                }
                std::vector<size_t> allowed;
                for (size_t ci = 0; ci < allChecks.size(); ++ci) {
                    if (!filledChecks.count(checkKey(allChecks[ci])) && policy.Allows(allChecks[ci], item)) {
                        allowed.push_back(ci);
                    }
                }
                if (allowed.empty()) {
                    unrestricted.push_back(item);
                    continue;
                }
                const size_t pick = allowed[rng.below(static_cast<uint32_t>(allowed.size()))];
                placements.push_back({ allChecks[pick], item });
                filledChecks.insert(checkKey(allChecks[pick]));
            }
            fastFillItems = std::move(unrestricted);
        }
        // Advancement goes first so the truncatable tail is junk; stable_partition is RNG-free.
        auto dropFrom = std::stable_partition(fastFillItems.begin(), fastFillItems.end(),
                                              [](const CwItem& i) { return i.advancement; });
        const size_t mustPlace = static_cast<size_t>(dropFrom - fastFillItems.begin());
        size_t ji = 0;
        for (size_t ci = 0; ci < allChecks.size() && ji < fastFillItems.size(); ++ci) {
            if (filledChecks.count(checkKey(allChecks[ci])))
                continue;
            size_t pick = ji;
            while (pick < fastFillItems.size() && !policy.Allows(allChecks[ci], fastFillItems[pick]))
                ++pick;
            if (pick == fastFillItems.size())
                continue;
            std::swap(fastFillItems[ji], fastFillItems[pick]);
            placements.push_back({ allChecks[ci], fastFillItems[ji++] });
            filledChecks.insert(checkKey(allChecks[ci]));
        }
        // Items equal checks per game after balancing, so any leftover is an arithmetic bug; nothing is discarded here.
        size_t unfilled = 0;
        for (const auto& c : allChecks)
            if (!filledChecks.count(checkKey(c)))
                ++unfilled;
        size_t leftoverBad = 0;
        std::string leftoverNames;
        for (size_t k = ji; k < fastFillItems.size(); ++k) {
            if (fastFillItems[k].discardable())
                continue;
            ++leftoverBad;
            if (leftoverNames.size() < 300)
                leftoverNames += (leftoverNames.empty() ? "" : ", ") + fastFillItems[k].name + " [" +
                                 (fastFillItems[k].game == GAME_OOT ? "oot/" : "mm/") +
                                 CwCatName(fastFillItems[k].cat) + "]";
        }
        if (ji < fastFillItems.size() || unfilled > 0) {
            // Identical on every pass, so bail to the seed reroll instead of burning kMaxPasses.
            result.error = "pool/check imbalance — " + std::to_string(fastFillItems.size() - ji) +
                           " item(s) had no check left (" + std::to_string(leftoverBad) + " non-junk" +
                           (leftoverNames.empty() ? "" : ": " + leftoverNames) + "), " + std::to_string(unfilled) +
                           " check(s) unfilled, placed " + std::to_string(ji) + " of which " +
                           std::to_string(mustPlace) + " were must-place";
            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << result.error << "\n";
            return result;
        }

        {
            auto residual = expectedItems;
            size_t unexpected = 0;
            for (const auto& p : placements) {
                if (presetCheckKeys.count(checkKey(p.check)))
                    continue;
                auto it = residual.find(std::string(p.item.game == GAME_OOT ? "oot:" : "mm:") + p.item.name);
                if (it == residual.end())
                    ++unexpected;
                else
                    --it->second;
            }
            size_t missingBad = 0, missingJunk = 0, overPlaced = 0;
            std::string missingNames;
            for (const auto& [key, n] : residual) {
                if (n < 0) {
                    overPlaced += static_cast<size_t>(-n);
                    continue;
                }
                if (n == 0)
                    continue;
                const auto info =
                    expectedInfo.count(key) ? expectedInfo.at(key) : ExpectedInfo{ CwCat::UNKNOWN, false };
                (info.discardable ? missingJunk : missingBad) += static_cast<size_t>(n);
                if (!info.discardable && missingNames.size() < 300)
                    missingNames += (missingNames.empty() ? "" : ", ") + key + " x" + std::to_string(n) + " [" +
                                    CwCatName(info.cat) + "]";
            }
            if (missingBad > 0 || missingJunk > 0 || overPlaced > 0 || unexpected > 0) {
                result.error = missingBad > 0
                                   ? std::to_string(missingBad) + " non-junk pool item(s) never placed: " + missingNames
                                   : std::to_string(missingJunk) + " junk never placed, " + std::to_string(overPlaced) +
                                         " over-placed, " + std::to_string(unexpected) +
                                         " not from the pool (balancer arithmetic bug)";
                std::cerr << "[ComboShip] CrossWorldCombinedFill: INVARIANT VIOLATED — " << result.error << "\n";
                return result;
            }
        }

        if (GenCancelled(progress)) {
            result.error = "cancelled";
            return result;
        }
        // Junk-holding checks may be oracle-unreachable because the oracles under-model; they are logged, not failed.
        auto vf = reachableFixpoint({}, {});
        auto& ootFinal = vf.ootReachable;
        auto& mmFinal = vf.mmReachable;
        // Unreachable MM advancement is always fatal; OOT advancement only under ALL_REACHABLE.
        size_t mmAdvUnreachable = 0, ootAdvUnreachable = 0, junkUnreachable = 0, junkUnreachableOot = 0;
        for (const auto& p : placements) {
            if (lockedCheckKeys.count(checkKey(p.check)))
                continue;
            const auto& reach = p.check.game == GAME_OOT ? ootFinal : mmFinal;
            if (reach.count(p.check.name))
                continue;
            if (p.item.advancement) {
                size_t& stranded = p.item.game == GAME_MM ? mmAdvUnreachable : ootAdvUnreachable;
                if (stranded++ < 8) {
                    std::cerr << "[ComboShip]   stranded " << (p.item.game == GAME_MM ? "MM " : "OOT ") << p.item.name
                              << " at " << (p.check.game == GAME_MM ? "MM " : "OOT ") << p.check.name << "\n";
                }
            } else {
                ++junkUnreachable;
                if (p.check.game == GAME_OOT && junkUnreachableOot++ < 60)
                    std::cerr << "[ComboShip]   unreachable OOT " << p.check.name << "\n";
            }
        }
        if (junkUnreachableOot > 0) {
            std::map<std::string, int> held;
            for (const auto& name : vf.ootOwned)
                if (name.find("Wallet") != std::string::npos || name.find("Jabber") != std::string::npos)
                    ++held[name];
            for (const auto& [name, count] : held)
                std::cerr << "[ComboShip]   OOT logic holds " << name << " x" << count << "\n";
        }
        // Mirrors DefaultGanonMajoraGoal, duplicated to keep this header free of ComboPlaythrough.h.
        // NO_LOGIC accepts an OOT-unbeatable seed.
        const bool ootWin = ootFinal.count("Ganon") > 0;
        const bool mmWin = mmFinal.count("Moon Majora Pot 01") > 0;
        int reachablePieces = 0;
        if (goal.hunt) {
            for (const auto& p : placements) {
                if (CwIsTriforcePiece(p.item.game, p.item.name) &&
                    (p.check.game == GAME_OOT ? ootFinal : mmFinal).count(p.check.name)) {
                    ++reachablePieces;
                }
            }
        }
        bool goalOk = (ootAccess != OotAccess::BEATABLE_ONLY) ||
                      (goal.hunt ? reachablePieces >= goal.required : IsBossGoalMet(goal, ootWin, mmWin));
        bool needRetry = mmAdvUnreachable > 0 || (ootAccess == OotAccess::ALL_REACHABLE && ootAdvUnreachable > 0) ||
                         (ootAccess == OotAccess::BEATABLE_ONLY && !goalOk);
        if (needRetry) {
            std::string goalStr = ootAccess != OotAccess::BEATABLE_ONLY ? ""
                                  : goalOk                              ? " goal=ok"
                                  : goal.hunt ? " goal=UNBEATABLE(pieces=" + std::to_string(reachablePieces) + "/" +
                                                    std::to_string(goal.required) + ")"
                                              : " goal=UNBEATABLE(ganon=" + std::to_string(ootWin) +
                                                    " majora=" + std::to_string(mmWin) + ")";
            lastPassError = "validation failed — mmAdvUnreachable=" + std::to_string(mmAdvUnreachable) +
                            " ootAdvUnreachable=" + std::to_string(ootAdvUnreachable) + goalStr +
                            (mmAdvUnreachable > 0 ? " (MM items stranded: the portal closed mid-fill)" : "");
            std::cerr << "[ComboShip] CrossWorldCombinedFill: validation failed — mmAdvUnreachable=" << mmAdvUnreachable
                      << " ootAdvUnreachable=" << ootAdvUnreachable << goalStr << " (pass " << pass << ", " << passMs()
                      << " ms) — retrying\n";
            continue;
        }
        // Unreachable checks are layout-driven, so re-placing items cannot fix them; bail to the seed reroll.
        // MM is excluded because its oracle under-models 3 checks on every seed.
        if (ootAccess == OotAccess::ALL_REACHABLE && junkUnreachableOot > 0) {
            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << junkUnreachableOot
                      << " OOT check(s) unreachable under All Locations Reachable (pass " << pass
                      << ") — rerolling seed\n";
            result.error = "OOT All Locations Reachable violated (" + std::to_string(junkUnreachableOot) +
                           " unreachable checks) — reroll for a new entrance layout";
            return result;
        }
        // An MM start frees the OOT root, so a strayed itemless player must always be able to walk back in.
        if (mmStart && portalGated && !ootClosedFixpoint(placements, {}).portalOpen) {
            lastPassError = "MM start: the OOT->MM portal is not re-openable from an empty OOT start";
            std::cerr << "[ComboShip] CrossWorldCombinedFill: " << lastPassError << " (pass " << pass << ", "
                      << passMs() << " ms) — retrying\n";
            continue;
        }
        if (ootAdvUnreachable > 0)
            std::cout << "[ComboShip] CrossWorldCombinedFill: " << ootAdvUnreachable
                      << " OOT advancement item(s) on unreachable checks — tolerated (relaxed OOT mode)\n";

        std::cout << "[ComboShip] CrossWorldCombinedFill: all " << advItems.size()
                  << " advancement items reachable from scratch (pass " << pass << ", " << passMs() << " ms; "
                  << junkItems.size() << " junk, " << junkUnreachable
                  << " junk on oracle-unreachable checks [oot=" << junkUnreachableOot
                  << " mm=" << (junkUnreachable - junkUnreachableOot) << "]; pool balanced: " << trimmedTotal
                  << " trimmed, " << paddedTotal << " padded)\n";
        std::cout << "[ComboShip] CrossWorldCombinedFill: oracle queries — oot " << ootStats.count << "x/"
                  << ootStats.ms << " ms, mm " << mmStats.count << "x/" << mmStats.ms << " ms\n";
        fillOk = true;
    }

    if (!fillOk) {
        result.error = "assumed fill failed after " + std::to_string(kMaxPasses) +
                       " passes; last cause: " + (lastPassError.empty() ? "unknown (see log)" : lastPassError);
        return result;
    }

    if (!forcedPlacements.empty()) {
        placements.insert(placements.end(), forcedPlacements.begin(), forcedPlacements.end());
        std::cout << "[ComboShip] CrossWorldCombinedFill: " << forcedPlacements.size()
                  << " forced placement(s) applied (e.g. Link's Pocket)\n";
    }

    if (progress) {
        progress->phase.store(3);
        progress->placed.store(0);
        progress->total.store(0);
    }

    // MM resolves its junk placeholder at pickup from the MM check id, which an OOT check lacks, so it is baked here.
    // Own RNG stream seeded per check: never draws from the fill's and never depends on order.
    {
        std::sort(mmJunkNames.begin(), mmJunkNames.end());
        mmJunkNames.erase(std::unique(mmJunkNames.begin(), mmJunkNames.end()), mmJunkNames.end());
        size_t unbaked = 0;
        for (auto& p : placements) {
            if (p.check.game != GAME_OOT || p.item.game != GAME_MM || p.item.name != kMmJunkName)
                continue;
            if (mmJunkNames.empty()) {
                ++unbaked;
                continue;
            }
            CwRng jr((static_cast<uint64_t>(masterSeed) << 32) ^ CwHashName(p.check.name) ^ 0x4A554E4BULL);
            p.item.name = mmJunkNames[jr.below(static_cast<uint32_t>(mmJunkNames.size()))];
        }
        if (unbaked > 0)
            std::cout << "[ComboShip] junk bake skipped for " << unbaked
                      << " cross placements: 2ship.dll reported no junkPool" << std::endl;
    }

    nlohmann::json spoiler;
    spoiler["masterSeed"] = masterSeed;
    spoiler["mode"] = "combined-logic assumed-fill";
    spoiler["startingGame"] = mmStart ? "MM" : "OOT";
    spoiler["fillStats"] = { { "advancementItems", static_cast<uint32_t>(advItems.size()) },
                             { "junkItems", static_cast<uint32_t>(junkItems.size()) },
                             { "poolTrimmed", static_cast<uint32_t>(trimmedTotal) },
                             { "poolPadded", static_cast<uint32_t>(paddedTotal) },
                             { "checks", static_cast<uint32_t>(allChecks.size()) },
                             { "passes", passesUsed } };
    spoiler["sharedItems"] = SharedKeysFromMask(effectiveSharedMask | (sharedMask & SharedLiveStateMask()));
    spoiler["modFamilies"] = EncodeModPairs(sharedPairs);
    spoiler["plentifulModItems"] = plentifulModItems;

    nlohmann::json ootPlacements = nlohmann::json::object();
    nlohmann::json mmPlacements = nlohmann::json::object();
    nlohmann::json foreignMarkers = nlohmann::json::array();

    for (const auto& p : placements) {
        if (p.check.game == GAME_OOT) {
            ootPlacements[p.check.name] = p.item.name;
        } else {
            mmPlacements[p.check.name] = p.item.name;
        }
        if (p.check.game != p.item.game) {
            nlohmann::json marker = { { "checkGame", p.check.game == GAME_OOT ? "oot" : "mm" },
                                      { "checkName", p.check.name },
                                      { "itemGame", p.item.game == GAME_OOT ? "oot" : "mm" },
                                      { "itemName", p.item.name },
                                      { "advancement", p.item.advancement } };
            // UNKNOWN is omitted so consumers use the advancement fallback.
            if (p.item.cat != CwCat::UNKNOWN)
                marker["category"] = CwCatName(p.item.cat);
            foreignMarkers.push_back(std::move(marker));
        }
    }

    spoiler["ootCount"] = static_cast<uint32_t>(ootPlacements.size());
    spoiler["oot"] = ootPlacements;
    spoiler["mmCount"] = static_cast<uint32_t>(mmPlacements.size());
    spoiler["mm"] = mmPlacements;
    spoiler["foreign"] = foreignMarkers;
    // Forced checks are owned at start and absent from the dump's fixed[], so the spoiler names them for CrossHints.
    nlohmann::json startKnown = nlohmann::json::array();
    for (const auto& fp : forcedPlacements)
        startKnown.push_back(
            { { "checkGame", fp.check.game == GAME_OOT ? "oot" : "mm" }, { "checkName", fp.check.name } });
    spoiler["startKnown"] = std::move(startKnown);

    for (const auto& p : placements) {
        // A foreign item's name lives in the other game's namespace and baked junk names collide with real OOT items.
        if (p.check.game != p.item.game)
            continue;
        if (p.check.game == GAME_OOT) {
            ootOracle.PlaceItem(p.check.name.c_str(), p.item.name.c_str());
        } else {
            mmOracle.PlaceItem(p.check.name.c_str(), p.item.name.c_str());
        }
    }

    result.spoilerJson = spoiler.dump(2);
    result.success = true;
    return result;
}

inline std::string CrossWorldGenerateSpoiler(const std::string& sohDumpJson, const std::string& mmDumpJson,
                                             uint32_t masterSeed) {
    nlohmann::json spoiler;
    spoiler["masterSeed"] = masterSeed;
    spoiler["mode"] = "no-logic native-only (phase1)";

    auto doGame = [&](const char* key, const std::string& dumpJson, uint32_t seed) {
        nlohmann::json out = nlohmann::json::object();
        try {
            auto d = nlohmann::json::parse(dumpJson);
            std::vector<std::string> checkNames, poolItems;
            for (auto& c : d.value("checks", nlohmann::json::array())) {
                std::string n = c.value("name", std::string{});
                if (!n.empty())
                    checkNames.push_back(n);
            }
            if (d.contains("pool")) {
                for (auto& it : d["pool"]) {
                    std::string n = it.value("name", std::string{});
                    if (!n.empty())
                        poolItems.push_back(n);
                }
            } else {
                for (auto& c : d.value("checks", nlohmann::json::array())) {
                    std::string v = c.value("vanillaItem", std::string{});
                    if (!v.empty())
                        poolItems.push_back(v);
                }
            }
            for (auto& f : d.value("fixed", nlohmann::json::array())) {
                std::string chk = f.value("check", std::string{});
                std::string item = f.value("item", std::string{});
                if (!chk.empty() && !item.empty())
                    out[chk] = item;
            }
            std::vector<std::string> shuffled = poolItems;
            CwRng rng(seed);
            cwShuffle(shuffled, rng);
            if (shuffled.size() != checkNames.size())
                std::cerr << "[ComboShip] CrossWorldGenerateSpoiler: " << key << " pool/check mismatch — "
                          << shuffled.size() << " items vs " << checkNames.size() << " checks\n";
            for (size_t i = 0; i < checkNames.size() && i < shuffled.size(); ++i) {
                out[checkNames[i]] = shuffled[i];
            }
            spoiler[std::string(key) + "Count"] = static_cast<uint32_t>(checkNames.size());
        } catch (...) { spoiler[std::string(key) + "Error"] = true; }
        spoiler[key] = out;
    };

    doGame("oot", sohDumpJson, masterSeed ^ 0x4F4F5400u);
    doGame("mm", mmDumpJson, masterSeed ^ 0x4D4D0000u);

    return spoiler.dump(2);
}

} // namespace ComboRando
