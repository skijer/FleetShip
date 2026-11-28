#include "FleetSaveMenu.h"
#include "../archipelago/ApPaths.h"
#include "gui/ComboWidgetStyle.h"
#include "rando/SharedItems.h"
#include "rando/CrossWorldRando.h"
#include "rando/MmEntryGate.h"

#include <cstring>
#include <ship/GameRegistry.h>
#include <ship/config/ConsoleVariable.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>
#include <span>
#include <string_view>
#include <tuple>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace {
struct CrossPoolChoice {
    const char* key;
    const char* label;
};
enum CrossPoolIndex {
    kPoolDungeons,
    kPoolExpanded,
    kPoolInteriors,
    kPoolShops,
    kPoolDoorInteriors,
    kPoolGrottos,
    kPoolBosses,
    kPoolDecoupled
};
constexpr std::array<CrossPoolChoice, 8> kCrossPools = { {
    { "dungeons", "Cross-Game Dungeon Entrances" },
    { "expanded", "Expanded Dungeon Pool" },
    { "interiors", "Cross-Game Interiors" },
    { "shops", "Cross-Game Shops" },
    { "doorInteriors", "Cross-Game Door Interiors" },
    { "grottos", "Cross-Game Grottos and Graves" },
    { "bosses", "Cross-Game Boss Rooms" },
    { "decoupled", "Decoupled Cross-Game Dungeons" },
} };

bool IsCrossPoolLocked(size_t index, const std::array<bool, kCrossPools.size()>& chosen) {
    if (index == kPoolExpanded || index == kPoolDecoupled) {
        return !chosen[kPoolDungeons];
    }
    return index == kPoolDoorInteriors && chosen[kPoolInteriors];
}

void Write(const std::filesystem::path& path, const nlohmann::json& json) {
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream file(temporary);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file << json.dump(2);
        file.flush();
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("Could not save the Fleet file.");
    }
#else
    std::filesystem::rename(temporary, path);
#endif
}

enum class PoolCategory { Major, Masks, DungeonRewards, Collectables, Skills, Custom, Souls, Others, Count };

constexpr std::array<const char*, static_cast<size_t>(PoolCategory::Count)> kPoolCategoryLabels = {
    "Major Items", "Masks", "Dungeon Rewards", "Collectables", "Skills", "Custom Items", "Souls", "Others"
};

std::string Lowercase(std::string text);

bool ContainsAny(const std::string& text, std::initializer_list<std::string_view> words) {
    return std::any_of(words.begin(), words.end(),
                       [&](std::string_view word) { return text.find(word) != std::string::npos; });
}

bool IsSongName(const std::string& lowered) {
    return ContainsAny(lowered, { "song", "minuet", "bolero", "serenade", "requiem", "nocturne", "prelude", "lullaby",
                                  "sonata", "bossa nova", "elegy", "oath to order", "ballad", "fugue" });
}

bool IsSkillName(const std::string& shown) {
    static const std::set<std::string> kExactSkills = { "climb",       "crawl", "grab",           "speak",
                                                        "open chests", "swim",  "ability to swim" };
    return kExactSkills.contains(shown) ||
           ContainsAny(shown, { "jabber nut", "button", "progressive magic", "magic meter", "scale", "strength",
                                "power bracelet", "goron bracelet", "gauntlet", "spin attack" });
}

bool IsCollectedAlongTheWay(const std::string& shown) {
    if (shown == "room key" || shown == "eyeball frog") {
        return false;
    }
    return shown.ends_with(" key") || shown.ends_with(" map") || shown.ends_with(" frog") ||
           ContainsAny(shown, { "key ring", "small key", "compass", "silver", "owl statue", "tingle's" });
}

bool IsFiller(const nlohmann::json& item, const std::string& shown) {
    return item.value("group", std::string{}) == "traps" || item.value("category", std::string{}) == "junk" ||
           shown.starts_with("bombchus (") || shown == "huge rupee";
}

bool IsCustomItemKey(const std::string& name) {
    return name.find('.') != std::string::npos && !name.starts_with("mm.") && !name.starts_with("oot.") &&
           !name.starts_with("nei.infinite.") && !name.starts_with("nei.bag.");
}

PoolCategory ClassifyPoolItem(const nlohmann::json& item) {
    const std::string name = item.value("name", std::string{});
    const std::string shown = Lowercase(item.value("displayName", name));
    const std::string lowered = Lowercase(name) + " " + shown;
    const std::string category = item.value("category", std::string{});
    const std::string group = item.value("group", std::string{});
    if (IsFiller(item, shown)) {
        return PoolCategory::Others;
    }
    if (ContainsAny(lowered, { "soul" })) {
        return PoolCategory::Souls;
    }
    if (ComboRando::IsDungeonRewardName(name) || ContainsAny(lowered, { "medallion", "spiritual_stone", "remains" })) {
        return PoolCategory::DungeonRewards;
    }
    if (IsCustomItemKey(name) && IsSongName(lowered)) {
        return PoolCategory::Custom;
    }
    if (IsSongName(lowered) || category == "token" || group == "health" || group == "keys" ||
        category == "strayFairy" || ContainsAny(lowered, { "triforce", "heart" }) || IsCollectedAlongTheWay(shown)) {
        return PoolCategory::Collectables;
    }
    if (category == "mask" || ContainsAny(lowered, { "mask", "bunny hood", "captain's hat", "postman's hat" })) {
        return PoolCategory::Masks;
    }
    if (group == "abilities" || IsSkillName(shown)) {
        return PoolCategory::Skills;
    }
    if (IsCustomItemKey(name)) {
        return PoolCategory::Custom;
    }
    return PoolCategory::Major;
}

constexpr std::string_view kMajorItemOrder[] = {
    "slingshot",
    "bomb bag",
    "bombchu",
    "bow",
    "fire arrow",
    "din's fire",
    "ice arrow",
    "farore's wind",
    "hookshot",
    "boomerang",
    "lens of truth",
    "magic bean",
    "hammer",
    "light arrow",
    "nayru's love",
    "ocarina",
    "roc's feather",
    "pictograph box",
    "powder keg",
    "fishing pole",
    "bottle",
    "kokiri sword",
    "master sword",
    "knife",
    "goron sword",
    "biggoron",
    "sword",
    "shield",
    "tunic",
    "boots",
    "child wallet",
    "wallet",
    "infinite wallet",
    "stick capacity",
    "infinite deku sticks",
    "nut capacity",
    "infinite deku nuts",
    "bombchu bag",
    "infinite bombchus",
    "infinite bomb bag",
    "quiver",
    "infinite quiver",
    "infinite deku seeds",
    "infinite magic",
    "capacity",
    "weird egg",
    "chicken",
    "zelda's letter",
    "pocket egg",
    "pocket cucco",
    "cojiro",
    "odd mushroom",
    "odd potion",
    "poacher's saw",
    "broken goron's sword",
    "prescription",
    "eyeball frog",
    "eyedrops",
    "claim check",
    "moon's tear",
    "title deed",
    "room key",
    "letter to kafei",
    "pendant",
    "letter to mama",
};

constexpr std::string_view kMaskOrder[] = {
    "postman's hat", "all-night",     "blast mask",   "stone mask",    "great fairy's mask", "deku mask",
    "keaton",        "bremen",        "bunny hood",   "don gero",      "mask of scents",     "goron mask",
    "romani's mask", "circus leader", "kafei's mask", "couple's mask", "mask of truth",      "zora mask",
    "kamaro",        "gibdo",         "garo's mask",  "captain's hat", "giant's mask",       "fierce deity",
    "skull mask",    "spooky mask",   "gerudo mask",
};

constexpr std::string_view kRewardOrder[] = {
    "kokiri's emerald", "goron's ruby",     "zora's sapphire", "forest medallion", "fire medallion", "water medallion",
    "shadow medallion", "spirit medallion", "light medallion", "odolwa",           "goht",           "gyorg",
    "twinmold",
};

constexpr std::string_view kCollectableOrder[] = {
    "zelda's lullaby",
    "epona's song",
    "saria's song",
    "sun's song",
    "song of time",
    "song of storms",
    "minuet",
    "bolero",
    "serenade",
    "requiem",
    "nocturne",
    "prelude",
    "sonata",
    "goron lullaby",
    "bossa nova",
    "elegy",
    "oath to order",
    "song of healing",
    "song of soaring",
    "inverted song of time",
    "song of double time",
    "ballad",
    "fugue",
    "command melody",
    "heart container",
    "piece of heart",
    "double defense",
    "gold skulltula token",
    "swamp gold skulltula",
    "ocean gold skulltula",
    "triforce",
};

constexpr std::string_view kSkillOrder[] = {
    "strength",      "gauntlet",     "bracelet",    "scale",         "magic",       "spin attack",
    "a button",      "c up",         "c down",      "c left",        "c right",     "climb",
    "crawl",         "open chests",  "swim",        "grab",          "speak",       "hylian jabber",
    "kokiri jabber", "goron jabber", "zora jabber", "gerudo jabber", "deku jabber", "terminan jabber",
};

constexpr std::string_view kSoulOrder[] = {
    "gohma's soul",     "king dodongo",   "barinade",      "phantom ganon",  "volvagia",     "morpha",
    "bongo bongo",      "twinrova",       "ganon's soul",  "soul of odolwa", "soul of goht", "soul of gyorg",
    "soul of twinmold", "soul of majora", "majora's soul", "soul of",        "bean soul",
};

constexpr std::string_view kCustomTailOrder[] = {
    "rod", "somaria", "spring", "summer", "autumn", "winter", "tunic", "boots", "shield",
};

constexpr std::string_view kDungeonOrder[] = {
    "great deku tree",  "dodongo's cavern", "jabu-jabu",      "forest temple",      "fire temple",
    "water temple",     "shadow",           "spirit",         "bottom of the well", "ice cavern",
    "training ground",  "gerudo fortress",  "ganon's castle", "chest game",         "clock town",
    "woodfall",         "snowhead",         "great bay",      "stone tower",        "pirates' fortress",
    "beneath the well", "ikana castle",     "secret shrine",  "swamp spider house", "ocean spider house",
};

constexpr size_t kDungeonItemKindCount = 7;
constexpr std::string_view kCollectedKinds[] = {
    "map",         "compass",    "small key", "key ring", "boss key", "silver",
    "stray fairy", "owl statue", "tingle's",  "frog",     "key",
};

constexpr std::string_view kFillerOrder[] = {
    "bombchu",
    "huge rupee",
    "trap",
};

size_t RankByWords(const std::string& shown, std::span<const std::string_view> words) {
    size_t rank = words.size();
    size_t longest = 0;
    for (size_t index = 0; index < words.size(); index++) {
        if (words[index].size() > longest && shown.find(words[index]) != std::string::npos) {
            rank = index;
            longest = words[index].size();
        }
    }
    return rank;
}

size_t CollectableRank(const std::string& shown) {
    const size_t named = RankByWords(shown, kCollectableOrder);
    if (named < std::size(kCollectableOrder)) {
        return named;
    }
    const size_t kind = RankByWords(shown, kCollectedKinds);
    const size_t dungeon = RankByWords(shown, kDungeonOrder);
    const bool isDungeonItem = kind < kDungeonItemKindCount && dungeon < std::size(kDungeonOrder);
    const size_t place = isDungeonItem ? dungeon : std::size(kDungeonOrder);
    return std::size(kCollectableOrder) + place * (std::size(kCollectedKinds) + 1) + kind;
}

size_t CustomItemRank(const std::string& shown) {
    const size_t tail = RankByWords(shown, kCustomTailOrder);
    return tail == std::size(kCustomTailOrder) ? 0 : tail + 1;
}

size_t PoolSortRank(PoolCategory category, const std::string& shown) {
    switch (category) {
        case PoolCategory::Major:
            return RankByWords(shown, kMajorItemOrder);
        case PoolCategory::Masks:
            return RankByWords(shown, kMaskOrder);
        case PoolCategory::DungeonRewards:
            return RankByWords(shown, kRewardOrder);
        case PoolCategory::Collectables:
            return CollectableRank(shown);
        case PoolCategory::Skills:
            return RankByWords(shown, kSkillOrder);
        case PoolCategory::Souls:
            return RankByWords(shown, kSoulOrder);
        case PoolCategory::Others:
            return RankByWords(shown, kFillerOrder);
        case PoolCategory::Custom:
            return CustomItemRank(shown);
        default:
            return 0;
    }
}

struct SongTint {
    std::string_view name;
    std::array<float, 4> tint;
};

constexpr SongTint kMmSongTints[] = {
    { "sonata of awakening", { 0.4f, 1.0f, 0.4f, 1.0f } },  { "goron lullaby", { 1.0f, 0.45f, 0.25f, 1.0f } },
    { "new wave bossa nova", { 0.3f, 0.55f, 1.0f, 1.0f } }, { "elegy of emptiness", { 1.0f, 0.65f, 0.2f, 1.0f } },
    { "oath to order", { 0.95f, 0.95f, 0.95f, 1.0f } },     { "song of healing", { 1.0f, 0.55f, 0.8f, 1.0f } },
    { "song of soaring", { 1.0f, 1.0f, 0.6f, 1.0f } },      { "inverted song of time", { 0.55f, 0.85f, 1.0f, 1.0f } },
    { "song of double time", { 0.4f, 0.9f, 0.9f, 1.0f } },
};

const SongTint* FindMmSongTint(const std::string& shown) {
    const auto found = std::find_if(std::begin(kMmSongTints), std::end(kMmSongTints),
                                    [&](const SongTint& song) { return shown.find(song.name) != std::string::npos; });
    return found == std::end(kMmSongTints) ? nullptr : found;
}

int PoolCopies(const nlohmann::json& item) {
    return item.value("shuffled", 0) + item.value("fixed", 0);
}

nlohmann::json::iterator FindPoolItem(nlohmann::json& pool, const std::string& game, const char* name) {
    return std::find_if(pool.begin(), pool.end(), [&](const nlohmann::json& item) {
        return item.value("game", std::string{}) == game && item.value("name", std::string{}) == name;
    });
}

void FoldTierAliases(nlohmann::json& pool) {
    for (const ComboRando::TierAlias& tier : ComboRando::kTierAliases) {
        const std::string game = tier.game == ComboRando::GAME_OOT ? "oot" : "mm";
        auto kept = FindPoolItem(pool, game, tier.alias);
        auto folded = FindPoolItem(pool, game, tier.anchor);
        if (kept == pool.end() || folded == pool.end()) {
            continue;
        }
        if (PoolCopies(*folded) > PoolCopies(*kept)) {
            std::swap(kept, folded);
        }
        (*kept)["shuffled"] = kept->value("shuffled", 0) + folded->value("shuffled", 0);
        (*kept)["fixed"] = kept->value("fixed", 0) + folded->value("fixed", 0);
        (*kept)["shared"] = kept->value("shared", false) || folded->value("shared", false);
        pool.erase(folded);
    }
}

struct UpgradeTier {
    const char* name;
    const char* onlyWith = nullptr;
};

struct UpgradeChain {
    const char* progressive;
    std::vector<UpgradeTier> tiers;
    std::vector<const char*> firsts;
    std::vector<const char*> lasts;
};

constexpr const char* kInfiniteTier = "gRandoSettings.InfiniteUpgrades";

const std::vector<UpgradeChain> kUpgradeChains = {
    { "Progressive Wallet",
      { { "Child Wallet (99)", "gRandoSettings.ShuffleChildWallet" },
        { "Adult's Wallet (200)" },
        { "Giant's Wallet (500)" },
        { "Tycoon's Wallet (999)", "gRandoSettings.IncludeTycoonWallet" },
        { "Infinite Rupees", kInfiniteTier } },
      { "Child Wallet" },
      { "Infinite Wallet" } },
    { "Progressive Bow",
      { { "Bow (30)" }, { "Quiver (40)" }, { "Quiver (50)" }, { "Infinite Arrows", kInfiniteTier } },
      {},
      { "Infinite Quiver" } },
    { "Progressive Slingshot",
      { { "Slingshot (30)" }, { "Bullet Bag (40)" }, { "Bullet Bag (50)" }, { "Infinite Seeds", kInfiniteTier } },
      {},
      { "Infinite Deku Seeds" } },
    { "Progressive Bomb Bag",
      { { "Bomb Bag (20)" }, { "Bomb Bag (30)" }, { "Bomb Bag (40)" }, { "Infinite Bombs", kInfiniteTier } },
      {},
      { "Infinite Bomb Bag" } },
    { "Bombchu Bag",
      { { "Bombchu Bag (20)" },
        { "Bombchu Bag (30)" },
        { "Bombchu Bag (50)" },
        { "Infinite Bombchus", kInfiniteTier } },
      {},
      { "Infinite Bombchus" } },
    { "Progressive Stick Capacity",
      { { "Deku Stick Bag (10)", "gRandoSettings.ShuffleDekuStickBag" },
        { "Deku Sticks (20)" },
        { "Deku Sticks (30)" },
        { "Infinite Deku Sticks", kInfiniteTier } },
      {},
      { "Infinite Deku Sticks" } },
    { "Progressive Nut Capacity",
      { { "Deku Nut Bag (20)", "gRandoSettings.ShuffleDekuNutBag" },
        { "Deku Nuts (30)" },
        { "Deku Nuts (40)" },
        { "Infinite Deku Nuts", kInfiniteTier } },
      {},
      { "Infinite Deku Nuts" } },
    { "Progressive Magic Meter",
      { { "Magic Meter" }, { "Double Magic" }, { "Infinite Magic", kInfiniteTier } },
      {},
      { "Infinite Magic" } },
    { "Strength Upgrade",
      { { "Power Bracelet", "gRandoSettings.ShuffleGrab" },
        { "Goron's Bracelet" },
        { "Silver Gauntlets" },
        { "Golden Gauntlets" } },
      {},
      {} },
    { "Progressive Scale", { { "Silver Scale" }, { "Golden Scale" } }, {}, {} },
    { "Progressive Hookshot", { { "Hookshot" }, { "Longshot" } }, {}, {} },
    { "Progressive Ocarina", { { "Fairy Ocarina" }, { "Ocarina of Time" } }, {}, {} },
    { "Progressive Goron Sword", { { "Giant's Knife" }, { "Biggoron's Sword" }, { "Great Fairy's Sword" } }, {}, {} },
};

bool IsJunkItem(const nlohmann::json& item) {
    const std::string shown = Lowercase(item.value("displayName", item.value("name", std::string{})));
    return item.value("junk", false) || shown.starts_with("buy ");
}

const char* GameLabel(const std::string& game) {
    return game == "oot" ? "OoT" : "MM";
}

constexpr int kOotStartIndex = 0;

constexpr auto kSettleTime = std::chrono::milliseconds(300);

constexpr std::array<const char*, 4> kSongPlacementKeys = { "native", "songLocations", "dungeonRewards", "anywhere" };

constexpr int kKeyRingDungeons = 14;
constexpr int kKeyRingModeCount = 2;
constexpr const char* kShuffleKokiriSwordCvar = "gRandoSettings.ShuffleKokiriSword";
constexpr const char* kMmSwordName = "Progressive Sword";
constexpr const char* kMmShieldName = "Hero's Shield";
constexpr std::array<const char*, 4> kSongPlacementLabels = { "Native", "Song Locations", "Dungeon Rewards",
                                                              "Anywhere" };

struct OwnGameCategory {
    const char* key;
    const char* label;
    const char* tooltip;
};
constexpr std::array<OwnGameCategory, 7> kOwnGameCategories = { {
    { "smallKey", "Own-Game Small Keys", "Small keys only land in checks of their own game." },
    { "bossKey", "Own-Game Boss Keys", "Boss keys only land in checks of their own game." },
    { "mapCompass", "Own-Game Maps and Compasses", "Maps and compasses only land in checks of their own game." },
    { "strayFairy", "Own-Game Stray Fairies", "Majora's Mask only: stray fairies only land in Majora's Mask checks." },
    { "dungeonReward", "Own-Game Dungeon Rewards",
      "Spiritual stones, medallions and boss remains only land in checks of their own game." },
    { "triforce", "Own-Game Triforce Pieces",
      "Triforce Hunt only: each game's pieces only land in checks of their own game." },
    { "gerudoKey", "Own-Game Gerudo Fortress Keys", "Gerudo Fortress keys only land in Ocarina of Time checks." },
} };

size_t OwnGameIndex(std::string_view key) {
    for (size_t index = 0; index < kOwnGameCategories.size(); index++) {
        if (key == kOwnGameCategories[index].key) {
            return index;
        }
    }
    assert(!"unknown own-game category");
    return 0;
}

constexpr std::array<const char*, 3> kRewardPlacementKeys = { "native", "bossesThisGame", "bossesBothGames" };
constexpr int kRewardsBossesThisGame = 1;
constexpr int kOotEndOfDungeons = 1;
constexpr int kOotAnywhere = 5;
constexpr int kOotOwnDungeon = 2;
using OwnGameChoices = std::array<bool, kOwnGameCategories.size()>;

nlohmann::json OwnGameKeys(const OwnGameChoices& chosen) {
    nlohmann::json keys = nlohmann::json::array();
    for (size_t index = 0; index < chosen.size(); index++) {
        if (chosen[index]) {
            keys.push_back(kOwnGameCategories[index].key);
        }
    }
    return keys;
}

constexpr std::array<const char*, 2> kGoalTypes = { "Bosses", "Triforce Hunt" };
constexpr std::array<const char*, 2> kGoalModes = { "Beat both games", "Beat either game" };

struct PlandoState {
    const char* key;
    const char* label;
};
constexpr std::array<PlandoState, 3> kBulkStates = { {
    { "", "Random" },
    { "vanilla", "Vanilla" },
    { "junk", "Junk" },
} };

struct StartingEquivalent {
    const char* oot;
    const char* mm;
};
constexpr StartingEquivalent kStartingEquivalents[] = {
    { "Empty Bottle", "Empty Bottle" },
    { "Magic Bean Pack", "Magic Bean" },
};

std::optional<std::pair<std::string, std::string>> StartingCounterpart(const std::string& game,
                                                                       const std::string& name) {
    const bool oot = game == "oot";
    const std::string other = oot ? "mm" : "oot";
    for (int family = 0; family < ComboRando::SF_COUNT; family++) {
        const auto& definition = ComboRando::SharedFamilyByIndex(family);
        if (definition.mmHasItem && name == (oot ? definition.ootName : definition.mmName)) {
            return std::make_pair(other, std::string(oot ? definition.mmName : definition.ootName));
        }
    }
    for (const auto& equivalent : kStartingEquivalents) {
        if (name == (oot ? equivalent.oot : equivalent.mm)) {
            return std::make_pair(other, std::string(oot ? equivalent.mm : equivalent.oot));
        }
    }
    return std::nullopt;
}

std::shared_ptr<Ship::ConsoleVariable> GameVariables(const std::string& game) {
    const auto services = Ship::GameRegistry::Get(game);
    return services ? services->ConsoleVariables : nullptr;
}

bool DrawGameCheckbox(const char* label, const std::string& game, const char* cvar) {
    const auto variables = GameVariables(game);
    if (!variables) {
        return false;
    }
    bool value = variables->GetInteger(cvar, 0) != 0;
    if (!ImGui::Checkbox(label, &value)) {
        return false;
    }
    variables->SetInteger(cvar, value ? 1 : 0);
    variables->Save();
    return true;
}

bool DrawChoiceCombo(const char* id, const char* label, int& value, std::span<const char* const> choices) {
    value = std::clamp(value, 0, static_cast<int>(choices.size()) - 1);
    const std::string preview = std::string(label) + ": " + choices[value];
    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ComboRando::ComboMenu_PushCombobox(ComboRando::ComboMenu_ThemeColor());
    if (ImGui::BeginCombo(id, preview.c_str())) {
        for (int choice = 0; choice < static_cast<int>(choices.size()); choice++) {
            if (ImGui::Selectable(choices[choice], choice == value) && choice != value) {
                value = choice;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ComboRando::ComboMenu_PopCombobox();
    return changed;
}

bool DrawGameCombo(const char* label, const std::string& game, const char* cvar, std::span<const char* const> choices) {
    const auto variables = GameVariables(game);
    if (!variables) {
        return false;
    }
    int value = variables->GetInteger(cvar, 0);
    if (!DrawChoiceCombo((std::string("##") + cvar).c_str(), label, value, choices)) {
        return false;
    }
    variables->SetInteger(cvar, value);
    variables->Save();
    return true;
}

constexpr int kMaxStartingHearts = 20;

bool DrawOotHeartSlider(const char* format, const char* cvar, int minimum, int defaultHearts) {
    const auto variables = GameVariables("oot");
    if (!variables) {
        return false;
    }
    int hearts = variables->GetInteger(cvar, defaultHearts - minimum) + minimum;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ComboRando::ComboMenu_PushSlider(ComboRando::ComboMenu_ThemeColor());
    const bool changed =
        ImGui::SliderInt((std::string("##") + cvar).c_str(), &hearts, minimum, kMaxStartingHearts, format);
    ComboRando::ComboMenu_PopSlider();
    if (!changed) {
        return false;
    }
    variables->SetInteger(cvar, hearts - minimum);
    variables->Save();
    return true;
}

std::string Lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char letter) { return std::tolower(letter); });
    return text;
}

constexpr int kModPairRow = ComboRando::kNoSharedFamily;

std::string ShareLabel(const ComboRando::SharedFamilyDef& family) {
    constexpr std::string_view kHeading = "Shared ";
    const std::string_view label = family.label;
    return std::string(label.starts_with(kHeading) ? label.substr(kHeading.size()) : label);
}

std::string ModPairLabel(const nlohmann::json& pair) {
    const std::string oot = pair.value("ootLabel", std::string{});
    const std::string mm = pair.value("mmLabel", std::string{});
    return oot == mm ? oot : oot + " / " + mm;
}

using PassageChoices = std::array<bool, std::size(ComboRando::CrossDungeon::kPassages)>;

void DrawPassages(PassageChoices& chosen, const ImVec4& theme) {
    const auto count = std::count(chosen.begin(), chosen.end(), true);
    const std::string preview = count == 0 ? "None" : std::to_string(count) + " of " + std::to_string(chosen.size());
    ImGui::TextUnformatted("Cross-Game Passages");
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("10 of 10").x + ImGui::GetFrameHeight() +
                            ImGui::GetStyle().FramePadding.x * 4);
    ComboRando::ComboMenu_PushCombobox(theme);
    if (ImGui::BeginCombo("##passages", preview.c_str())) {
        for (size_t index = 0; index < chosen.size(); index++) {
            ImGui::Checkbox(ComboRando::CrossDungeon::kPassages[index].label, &chosen[index]);
        }
        ImGui::EndCombo();
    }
    ComboRando::ComboMenu_PopCombobox();
    ImGui::TextDisabled("Each passage is a hole beside both regions' entrances that leads into the other game.");
}

std::string PassageArgument(const PassageChoices& chosen) {
    std::string keys;
    for (size_t index = 0; index < chosen.size(); index++) {
        if (chosen[index]) {
            keys += (keys.empty() ? "" : ",") + std::string(ComboRando::CrossDungeon::kPassages[index].key);
        }
    }
    return keys;
}

constexpr int kGerudoFortressFree = 2;

bool IsOotOptionOff(const char* cvar) {
    const auto oot = GameVariables("oot");
    return oot && oot->GetInteger(cvar, 0) == 0;
}

bool StartsWithUnshuffledGerudoCard() {
    const auto oot = GameVariables("oot");
    return oot && oot->GetInteger("gRandoSettings.ShuffleGerudoToken", 0) == 0 &&
           oot->GetInteger("gRandoSettings.FortressCarpenters", 0) == kGerudoFortressFree;
}

// How SoH hands out an OoT item that its options keep out of the pool.
std::string OotUnshuffledNote(const std::string& displayName) {
    const std::string name = Lowercase(displayName);
    if (name == "gerudo membership card" && IsOotOptionOff("gRandoSettings.ShuffleGerudoToken")) {
        return StartsWithUnshuffledGerudoCard()
                   ? "Link starts with the Gerudo Membership Card: Gerudo Fortress is Free and the card is not "
                     "shuffled."
                   : "Gerudo Membership Card not shuffled: freeing the carpenters gives it.";
    }
    if (name == "progressive stick capacity" && IsOotOptionOff("gRandoSettings.ShuffleDekuStickBag")) {
        return "Deku Stick Bag not shuffled: there is no bag to find, the first Deku Sticks give room for 10.";
    }
    if (name == "progressive nut capacity" && IsOotOptionOff("gRandoSettings.ShuffleDekuNutBag")) {
        return "Deku Nut Bag not shuffled: there is no bag to find, the first Deku Nuts give room for 20.";
    }
    return {};
}

void DrawOotUnshuffledNotes() {
    static constexpr const char* kNotedItems[] = { "Gerudo Membership Card", "Progressive Stick Capacity",
                                                   "Progressive Nut Capacity" };
    for (const char* item : kNotedItems) {
        if (const std::string note = OotUnshuffledNote(item); !note.empty()) {
            ImGui::BulletText("%s", note.c_str());
        }
    }
}
} // namespace

struct PlacementChoice {
    const char* label;
    int value;
    bool isOwnGame = false;
    const char* rewards = "native";
};

struct DungeonItemRow {
    const char* label;
    const char* game;
    const char* cvar;
    int defaultValue;
    int anywhereValue;
    const char* ownGameKey;
    const char* tooltip;
    std::vector<PlacementChoice> choices;
};

struct EntranceChoice {
    const char* label;
    int native;
    std::vector<size_t> pools;
};
struct EntranceRow {
    const char* label;
    const char* cvar;
    const char* tooltip;
    std::vector<EntranceChoice> choices;
};

namespace {
const std::vector<EntranceRow> kEntranceRows = {
    { "Dungeon Entrances",
      "gRandoSettings.ShuffleDungeonsEntrances",
      "Own Game shuffles dungeon entrances within Ocarina of Time; Cross-Game mixes them with Majora's Mask's. "
      "+ Ganon includes Ganon's Castle.",
      { { "Off", 0, {} },
        { "Own Game", 1, {} },
        { "Own Game + Ganon", 2, {} },
        { "Cross-Game", 0, { kPoolDungeons } },
        { "Cross-Game + Ganon", 2, { kPoolDungeons } } } },
    { "Boss Entrances",
      "gRandoSettings.ShuffleBossEntrances",
      "Own Game shuffles boss rooms within Ocarina of Time; Cross-Game swaps boss rooms between both games.",
      { { "Off", 0, {} },
        { "Own Game (Age Restricted)", 1, {} },
        { "Own Game (Full)", 2, {} },
        { "Cross-Game", 0, { kPoolBosses } } } },
    { "Interior Entrances",
      "gRandoSettings.ShuffleInteriorsEntrances",
      "Own Game shuffles Ocarina of Time's interiors among themselves; Cross-Game Doors swaps door interiors between "
      "both games, Cross-Game every interior.",
      { { "Off", 0, {} },
        { "Own Game (Simple)", 1, {} },
        { "Own Game (All)", 2, {} },
        { "Cross-Game Doors", 0, { kPoolDoorInteriors } },
        { "Cross-Game", 0, { kPoolInteriors } } } },
    { "Grotto Entrances",
      "gRandoSettings.ShuffleGrottosEntrances",
      "Own Game shuffles Ocarina of Time's grottos; Cross-Game swaps grottos and graves between both games.",
      { { "Off", 0, {} }, { "Own Game", 1, {} }, { "Cross-Game", 0, { kPoolGrottos } } } },
    { "Shop Entrances",
      nullptr,
      "Cross-Game swaps shop entrances between both games.",
      { { "Off", 0, {} }, { "Cross-Game", 0, { kPoolShops } } } },
};

const std::vector<PlacementChoice> kOotDungeonItemChoices = { { "Start With", 0 },
                                                              { "Vanilla", 1 },
                                                              { "Own Dungeon", 2 },
                                                              { "Any Dungeon", 3 },
                                                              { "Overworld", 4 },
                                                              { "Anywhere: This Game", kOotAnywhere, true },
                                                              { "Anywhere: Both Games", kOotAnywhere } };

constexpr const char* kPlacementHelp =
    "\n\nOwn Dungeon, Any Dungeon and Overworld stay in the item's own game.\nAnywhere: This Game keeps each "
    "game's items in its own checks; Anywhere: Both Games lets them land in either game.";

const std::vector<DungeonItemRow> kDungeonItemRows = {
    { "Maps/Compasses", "oot", "gRandoSettings.StartingMapsCompasses", kOotOwnDungeon, kOotAnywhere, "mapCompass",
      "Where the maps and compasses of both games land.", kOotDungeonItemChoices },
    { "Small Key Shuffle", "oot", "gRandoSettings.Keysanity", kOotOwnDungeon, kOotAnywhere, "smallKey",
      "Where the small keys of both games land.", kOotDungeonItemChoices },
    { "Boss Key Shuffle", "oot", "gRandoSettings.BossKeysanity", kOotOwnDungeon, kOotAnywhere, "bossKey",
      "Where the boss keys of both games land.", kOotDungeonItemChoices },
    { "Shuffle Dungeon Rewards",
      "oot",
      "gRandoSettings.ShuffleDungeonReward",
      1,
      kOotAnywhere,
      "dungeonReward",
      "Where spiritual stones, medallions and boss remains land.\nBosses: This Game shuffles each game's rewards "
      "among its own bosses; Bosses: Both Games among the bosses of both games (Volvagia may give Goht's Remains).",
      { { "Vanilla", 0 },
        { "Bosses: This Game", kOotAnywhere, false, "bossesThisGame" },
        { "Bosses: Both Games", kOotAnywhere, false, "bossesBothGames" },
        { "Own Dungeon (OoT)", 2 },
        { "Any Dungeon (OoT)", 3 },
        { "Overworld (OoT)", 4 },
        { "Anywhere: This Game", kOotAnywhere, true },
        { "Anywhere: Both Games", kOotAnywhere } } },
    { "Gerudo Fortress Keys",
      "oot",
      "gRandoSettings.GerudoKeys",
      0,
      3,
      "gerudoKey",
      "Ocarina of Time only: where the Gerudo Fortress keys land.",
      { { "Vanilla", 0 },
        { "Any Dungeon", 1 },
        { "Overworld", 2 },
        { "Anywhere: This Game", 3, true },
        { "Anywhere: Both Games", 3 } } },
    { "Stray Fairies",
      "mm",
      "gRando.Options.RO_PLACEMENT_STRAY_FAIRIES",
      0,
      0,
      "strayFairy",
      "Majora's Mask only: where stray fairies land.",
      { { "Start With", 2 },
        { "Vanilla", 3 },
        { "Own Dungeon", 1 },
        { "Any Dungeon", 4 },
        { "Overworld", 5 },
        { "Anywhere: This Game", 0, true },
        { "Anywhere: Both Games", 0 } } },
};
} // namespace

FleetSaveMenu::FleetSaveMenu(std::filesystem::path directory, std::string oot, std::string mm,
                             std::filesystem::path saves)
    : GuiWindow("", true, "FleetFiles"), mDirectory(std::move(directory)),
      mRoot(saves.empty() ? mDirectory / "saves" / "fleet" : std::filesystem::absolute(saves)), mOot(std::move(oot)),
      mMm(std::move(mm)) {
    mPools.fill(true);
    mPools[kPoolDecoupled] = false;
    const auto seed = std::to_string(std::random_device{}());
    std::copy(seed.begin(), seed.end(), mSeed.begin());
    std::filesystem::create_directories(mRoot);
    LoadSlots();
    LoadFleetStarting();
    LoadShareChoices();
    LoadLogicChoices();
    LoadGeneralChoices();
    LoadPoolEdits();
    LoadPlando();
    RebuildShareRows(nlohmann::json::array());
    RefreshSeedFiles();
    for (const char* game : { "oot", "mm" }) {
        mLobbyDirectories[game] = (mRoot / "lobby" / game).string();
        std::filesystem::create_directories(std::filesystem::path(mLobbyDirectories[game]) /
                                            (std::string(game) == "oot" ? "Save" : "saves"));
    }
}

FleetSaveMenu::~FleetSaveMenu() {
    StopWorker();
}

bool FleetSaveMenu::Enabled() const {
    return mEnabled;
}

bool FleetSaveMenu::HasSlot(int slot) const {
    return mSlots.contains(std::to_string(slot));
}

void FleetSaveMenu::SetSnapshot(std::function<nlohmann::json(const std::string&)> snapshot) {
    mSnapshot = std::move(snapshot);
}

std::vector<std::string> FleetSaveMenu::BaseArguments() const {
    return { "--internal-worker", "--oot", mOot, "--mm", mMm };
}

void FleetSaveMenu::LoadSlots() {
    const auto file = mRoot / "slots.json";
    if (!std::filesystem::exists(file)) {
        return;
    }
    mSlots = nlohmann::json::parse(std::ifstream(file));
    if (!mSlots.is_object()) {
        throw std::runtime_error("Could not read the save list. Your files have been preserved.");
    }
    for (const auto& [slot, entry] : mSlots.items()) {
        if ((slot != "0" && slot != "1" && slot != "2") || !entry.at("seed").is_string() ||
            !entry.at("session").is_string()) {
            throw std::runtime_error("The save list contains an invalid entry. Your files have been preserved.");
        }
    }
}

bool FleetSaveMenu::OpenFiles() {
    if (!mEnabled) {
        return false;
    }
    if (!mPrepared) {
        mPrepared = true;
        try {
            std::filesystem::create_directories(mRoot);
            LoadSlots();
        } catch (const std::exception& error) {
            mSlotsValid = false;
            Fail(error.what());
        }
        std::cout << "[fleet] in-game file select opened" << std::endl;
    }
    return mActive;
}

const std::string& FleetSaveMenu::SaveDirectory(const std::string& game) const {
    return mLobbyDirectories.at(game);
}

std::string FleetSaveMenu::SaveFile(const std::string& game, int slot, bool backup) const {
    if (slot < 0 || slot > 2 || (game != "oot" && game != "mm")) {
        throw std::invalid_argument("Invalid Fleet save slot.");
    }
    std::filesystem::path root = SaveDirectory(game);
    const auto entry = mSlots.find(std::to_string(slot));
    if (entry != mSlots.end()) {
        root = std::filesystem::path(entry->at("session").get<std::string>()) / ("slot" + std::to_string(slot)) / game;
    }
    return (root / (game == "oot" ? "Save" : "saves") /
            ("file" + std::to_string(slot + 1) + (backup ? "backup" : "") + (game == "oot" ? ".sav" : ".json")))
        .string();
}

void FleetSaveMenu::OpenArchipelagoSetup(const std::string& slot) {
    std::strncpy(mArchipelagoSlot.data(), slot.c_str(), mArchipelagoSlot.size() - 1);
    mIsArchipelagoSetup = true;
    mSlot = -1;
    mActive = true;
    try {
        LoadCatalogue();
    } catch (const std::exception& error) { Fail(error.what()); }
}

bool FleetSaveMenu::SelectNative(int slot) {
    mNativeMode = false;
    if (!mEnabled || slot < 0 || slot > 2) {
        return false;
    }
    Select(slot);
    SeedFor(slot);
    if (mReady && mReady->fresh) {
        mActive = true;
    }
    if (!mReady) {
        mActive = true;
        try {
            LoadCatalogue();
        } catch (const std::exception& error) { Fail(error.what()); }
    }
    return true;
}

void FleetSaveMenu::LoadCatalogue() {
    if (mCatalogue.is_object() || mJob.Running()) {
        return;
    }
    auto args = BaseArguments();
    args.insert(args.end(), { "--catalogue", (mRoot / "options.json").string() });
    mJob.Start(mDirectory / kFleetExecutable, args, mRoot / "options.log");
    mLoadingCatalogue = true;
}

bool FleetSaveMenu::Manage(const std::string& operation, int from, int to) {
    if (from < 0 || from > 2) {
        throw std::invalid_argument("The selected save does not exist.");
    }
    if (!HasSlot(from)) {
        if ((operation != "erase" && operation != "copy") ||
            (operation == "copy" && (to < 0 || to > 2 || to == from || HasSlot(to)))) {
            throw std::invalid_argument("The copy destination must be an empty slot.");
        }
        const auto nonce = std::chrono::system_clock::now().time_since_epoch().count();
        const auto recovery = mRoot / "recovery" / std::to_string(nonce);
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> files;
        for (const char* game : { "oot", "mm" }) {
            for (bool backup : { false, true }) {
                const std::filesystem::path source = SaveFile(game, from, backup);
                if (!std::filesystem::exists(source)) {
                    continue;
                }
                const auto destination = operation == "copy" ? std::filesystem::path(SaveFile(game, to, backup))
                                                             : recovery / game / source.filename();
                if (std::filesystem::exists(destination)) {
                    throw std::invalid_argument("The destination slot already contains a save.");
                }
                files.emplace_back(source, destination);
            }
        }
        if (files.empty()) {
            throw std::invalid_argument("The selected save does not exist.");
        }
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> completed;
        try {
            for (const auto& [source, destination] : files) {
                std::filesystem::create_directories(destination.parent_path());
                if (operation == "copy") {
                    std::filesystem::copy_file(source, destination);
                } else {
                    std::filesystem::rename(source, destination);
                }
                completed.emplace_back(source, destination);
            }
        } catch (...) {
            for (auto entry = completed.rbegin(); entry != completed.rend(); ++entry) {
                std::error_code ignored;
                if (operation == "copy") {
                    std::filesystem::remove(entry->second, ignored);
                } else {
                    std::filesystem::rename(entry->second, entry->first, ignored);
                }
            }
            throw;
        }
        return true;
    }
    if (operation == "erase") {
        auto slots = mSlots;
        slots.erase(std::to_string(from));
        Write(mRoot / "slots.json", slots);
        mSlots = std::move(slots);
        return true;
    }
    if (operation != "copy" || to < 0 || to > 2 || to == from || mSlots.contains(std::to_string(to))) {
        throw std::invalid_argument("The copy destination must be an empty slot.");
    }
    const auto source = mSlots.at(std::to_string(from));
    const auto nonce = std::chrono::system_clock::now().time_since_epoch().count();
    const auto run = mRoot / ("slot" + std::to_string(to)) / std::to_string(nonce);
    const auto session = run / "session";
    const auto sourceSession = std::filesystem::path(source.at("session").get<std::string>());
    std::filesystem::create_directories(session);
    std::filesystem::copy_file(source.at("seed").get<std::string>(), run / "seed.json");
    std::filesystem::copy_file(sourceSession / "seed.json", session / "seed.json");
    const auto targetSlot = session / ("slot" + std::to_string(to));
    std::filesystem::copy(sourceSession / ("slot" + std::to_string(from)), targetSlot,
                          std::filesystem::copy_options::recursive);
    for (const char* game : { "oot", "mm" }) {
        const auto folder = targetSlot / game / (std::string(game) == "oot" ? "Save" : "saves");
        for (bool backup : { false, true }) {
            const std::string suffix =
                (backup ? "backup" : "") + std::string(std::string(game) == "oot" ? ".sav" : ".json");
            const auto oldFile = folder / ("file" + std::to_string(from + 1) + suffix);
            const auto newFile = folder / ("file" + std::to_string(to + 1) + suffix);
            if (std::filesystem::exists(oldFile)) {
                std::filesystem::rename(oldFile, newFile);
            }
        }
    }
    auto slots = mSlots;
    slots[std::to_string(to)] = { { "seed", (run / "seed.json").string() },
                                  { "session", session.string() },
                                  { "number", source.value("number", "") } };
    Write(mRoot / "slots.json", slots);
    mSlots = std::move(slots);
    return true;
}

void FleetSaveMenu::Fail(const std::string& error) {
    mReady.reset();
    mError = error;
    mNativeRequested = false;
    mActive = !mNativeMode;
}

bool FleetSaveMenu::GenerateNative(int slot) {
    if (!mEnabled || mJob.Running() || HasSlot(slot) || !SelectNative(slot)) {
        return false;
    }
    mReady.reset();
    mAutomated = false;
    if (!mSeedExplicit) {
        mSeed.fill(0);
    }
#ifdef FLEET_TEST_DRIVER
    if (!mSeedExplicit) {
        const std::string seed = "80302";
        std::copy(seed.begin(), seed.end(), mSeed.begin());
    }
#endif
    mNativeMode = mNativeRequested = true;
    mActive = false;
    mStartRequested = false;
    return true;
}

int FleetSaveMenu::GenerationState() const {
    if (mNativeRequested || mGenerating) {
        return 1;
    }
    if (mReady && mReady->fresh) {
        return 2;
    }
    return mError.empty() ? 0 : 3;
}

void FleetSaveMenu::Select(int slot) {
    if (mReady && mReady->slot != slot) {
        mReady.reset();
    }
    mError.clear();
    mSlot = slot;
    const auto entry = mSlots.find(std::to_string(slot));
    if (entry != mSlots.end()) {
        mReady = Ready{ entry->at("seed").get<std::string>(), entry->at("session").get<std::string>(), slot, false };
        mStartRequested = true;
    }
}

void FleetSaveMenu::Generate() {
    if (mShareChoices.IsBulkChoicePending()) {
        mGenerateWhenPoolArrives = true;
        return;
    }
    mReady.reset();
    mStartRequested = false;
    if (mSeed[0] == '\0') {
        const auto seed = std::to_string(std::random_device{}());
        std::copy(seed.begin(), seed.end(), mSeed.begin());
    }
    uint32_t seed = 0;
    const std::string text(mSeed.data());
    const auto result = std::from_chars(text.data(), text.data() + text.size(), seed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        throw std::runtime_error("The seed must be a number from 0 to 4294967295.");
    }
    if (mHunt && (mRequired < 1 || mTotal < mRequired || mTotal > ComboRando::kMaxComboTriforcePieces)) {
        throw std::runtime_error("The total piece count must meet the Triforce goal.");
    }
    if (mSlots.contains(std::to_string(mSlot))) {
        throw std::runtime_error("That slot already contains a save.");
    }
    const auto nonce = std::chrono::system_clock::now().time_since_epoch().count();
    mRun = mRoot / ("slot" + std::to_string(mSlot)) / std::to_string(nonce);
    auto args = GenerationArguments(mRun, text);
    args.insert(args.end(),
                { "--out", (mRun / "seed.json").string(), "--progress", (mRun / "progress.json").string() });
    mJob.Start(mDirectory / kFleetExecutable, args, mRun / "generation.log");
    mGenerating = true;
    mGenerationStart = std::chrono::steady_clock::now();
    mSeedProgress = nlohmann::json::object();
    mError.clear();
    std::cout << "[fleet] in-game generation started in slot " << mSlot << std::endl;
}

std::filesystem::path FleetSaveMenu::SeedFolder() const {
    return mDirectory / "Randomizer";
}

void FleetSaveMenu::RefreshSeedFiles() {
    const std::string chosen = mSeedChoice > 0 && mSeedChoice <= static_cast<int>(mSeedFiles.size())
                                   ? mSeedFiles[mSeedChoice - 1]
                                   : std::string{};
    mSeedFiles.clear();
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator(SeedFolder(), ignored)) {
        if (entry.path().extension() == ".json") {
            mSeedFiles.push_back(entry.path().filename().string());
        }
    }
    std::sort(mSeedFiles.begin(), mSeedFiles.end());
    const auto kept = std::find(mSeedFiles.begin(), mSeedFiles.end(), chosen);
    mSeedChoice = kept == mSeedFiles.end() ? 0 : static_cast<int>(kept - mSeedFiles.begin()) + 1;
}

std::string FleetSaveMenu::StoreSeedFile(const std::filesystem::path& source, const std::string& name) {
    std::filesystem::create_directories(SeedFolder());
    const std::filesystem::path base = std::filesystem::path(name).stem();
    std::filesystem::path target = SeedFolder() / name;
    for (int copy = 2; std::filesystem::exists(target); copy++) {
        if (std::filesystem::equivalent(source, target) ||
            nlohmann::json::parse(std::ifstream(source), nullptr, false) ==
                nlohmann::json::parse(std::ifstream(target), nullptr, false)) {
            return target.filename().string();
        }
        target = SeedFolder() / (base.string() + "-" + std::to_string(copy) + ".json");
    }
    std::filesystem::copy_file(source, target);
    return target.filename().string();
}

std::optional<std::string> FleetSaveMenu::AddDroppedSeed(const std::filesystem::path& file) {
    const auto seed = nlohmann::json::parse(std::ifstream(file), nullptr, false);
    if (!seed.is_object() || seed.value("fileType", std::string{}) != "ComboShipRandomizer") {
        return std::nullopt;
    }
    try {
        const std::string stored = StoreSeedFile(file, file.filename().string());
        RefreshSeedFiles();
        const auto found = std::find(mSeedFiles.begin(), mSeedFiles.end(), stored);
        mSeedChoice = found == mSeedFiles.end() ? 0 : static_cast<int>(found - mSeedFiles.begin()) + 1;
        mError.clear();
        if (mReady && mReady->fresh) {
            mReady.reset();
        }
        return "Seed " + stored + " loaded: Start Randomizer on an empty file plays it.";
    } catch (const std::exception& error) { return std::string("Could not load the seed: ") + error.what(); }
}

std::optional<FleetSaveMenu::Ready> FleetSaveMenu::SeedFor(int slot) {
    if (const auto ready = Generated(slot)) {
        return ready;
    }
    if (mSeedChoice == 0 || !mEnabled || slot < 0 || slot > 2 || HasSlot(slot) || mJob.Running()) {
        return std::nullopt;
    }
    try {
        Select(slot);
        UseSeedFile(SeedFolder() / mSeedFiles[mSeedChoice - 1]);
    } catch (const std::exception& error) {
        mSeedChoice = 0;
        Fail(error.what());
        return std::nullopt;
    }
    return Generated(slot);
}

void FleetSaveMenu::UseSeedFile(const std::filesystem::path& file) {
    if (HasSlot(mSlot)) {
        throw std::runtime_error("That file already contains a save.");
    }
    const auto seed = nlohmann::json::parse(std::ifstream(file), nullptr, false);
    if (!seed.is_object() || seed.value("fileType", std::string{}) != "ComboShipRandomizer") {
        throw std::runtime_error("That file is not a Fleet seed.");
    }
    const auto nonce = std::chrono::system_clock::now().time_since_epoch().count();
    mRun = mRoot / ("slot" + std::to_string(mSlot)) / std::to_string(nonce);
    std::filesystem::create_directories(mRun);
    std::filesystem::copy_file(file, mRun / "seed.json");
    const std::string number = std::to_string(seed.value("seed", uint32_t{ 0 }));
    mSeed.fill(0);
    std::copy(number.begin(), number.end(), mSeed.begin());
    mReady = Ready{ mRun / "seed.json", mRun / "session", mSlot, true };
    mResult = seed;
    mStartRequested = false;
    mError.clear();
}

void FleetSaveMenu::DrawSeedChoice(const ImVec4& theme) {
    if (mIsArchipelagoSetup) {
        DrawArchipelagoExport(theme);
        return;
    }
    const std::string preview = mSeedChoice == 0 ? "Generate New Seed" : mSeedFiles[mSeedChoice - 1];
    ImGui::SetNextItemWidth(260);
    ComboRando::ComboMenu_PushCombobox(theme);
    if (ImGui::BeginCombo("Seed##choice", preview.c_str())) {
        RefreshSeedFiles();
        if (ImGui::Selectable("Generate New Seed", mSeedChoice == 0)) {
            mSeedChoice = 0;
        }
        for (size_t index = 0; index < mSeedFiles.size(); index++) {
            if (ImGui::Selectable(mSeedFiles[index].c_str(), mSeedChoice == static_cast<int>(index) + 1)) {
                mSeedChoice = static_cast<int>(index) + 1;
            }
        }
        ImGui::EndCombo();
    }
    ComboRando::ComboMenu_PopCombobox();
    const bool generating = mSeedChoice == 0;
    if (generating) {
        ImGui::SetNextItemWidth(260);
        if (ImGui::InputTextWithHint("##seedNumber", "(blank = random)", mSeed.data(), mSeed.size(),
                                     ImGuiInputTextFlags_CharsDecimal)) {
            mSeedExplicit = mSeed[0] != '\0';
        }
        ImGui::SameLine();
    }
    const bool choosingFile = mEnabled && mActive && mSlot >= 0;
    ImGui::BeginDisabled(!choosingFile || (generating && !mCatalogue.is_object()));
    ComboRando::ComboMenu_PushButton(theme);
    if (ImGui::Button(generating ? "Generate" : "Use Seed")) {
        try {
            if (generating) {
                Generate();
            } else {
                UseSeedFile(SeedFolder() / mSeedFiles[mSeedChoice - 1]);
            }
        } catch (const std::exception& error) { Fail(error.what()); }
    }
    ComboRando::ComboMenu_PopButton();
    ImGui::EndDisabled();
    ImGui::TextDisabled(choosingFile ? ("File " + std::to_string(mSlot + 1)).c_str()
                                     : "Available on the file-select screen. Choose an empty file, then Randomizer.");
    ImGui::TextDisabled("Seeds live in %s. Drop a Fleet seed (.json) on the window to add it.",
                        SeedFolder().string().c_str());
    if (generating) {
        DrawArchipelagoExport(theme);
    }
}

void FleetSaveMenu::DrawArchipelagoExport(const ImVec4& theme) {
    ImGui::Spacing();
    ImGui::TextUnformatted("Archipelago");
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##archipelagoSlot", "Slot name", mArchipelagoSlot.data(), mArchipelagoSlot.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(mArchipelagoSlot[0] == '\0' || mArchipelagoJob.Running() || !mCatalogue.is_object());
    ComboRando::ComboMenu_PushButton(theme);
    if (ImGui::Button("Export for Archipelago")) {
        try {
            ExportArchipelago();
        } catch (const std::exception& error) { mArchipelagoStatus = error.what(); }
    }
    ComboRando::ComboMenu_PopButton();
    ImGui::EndDisabled();
    if (!mArchipelagoStatus.empty()) {
        ImGui::TextWrapped("%s", mArchipelagoStatus.c_str());
    }
    mArchipelagoActions.Draw(theme);
}

// Runs the same generation as a new file, then hands the seed to Archipelago instead of opening it.
void FleetSaveMenu::ExportArchipelago() {
    const auto nonce = std::chrono::system_clock::now().time_since_epoch().count();
    const std::filesystem::path run = mRoot / "archipelago" / std::to_string(nonce);
    const std::string seed = mSeed[0] != '\0' ? std::string(mSeed.data()) : std::to_string(std::random_device{}());
    auto args = GenerationArguments(run, seed);
    args.insert(args.end(), { "--archipelago-out", ArchipelagoFolder(mDirectory).string(), "--archipelago-player",
                              mArchipelagoSlot.data() });
    mArchipelagoJob.Start(mDirectory / kFleetExecutable, args, run / "archipelago.log");
    mArchipelagoStatus = "Exporting... Fleet measures the logic of every check, which takes a few minutes.";
    mArchipelagoActions.Withdraw();
}

nlohmann::json FleetSaveMenu::ComposeSettings() {
    if (mSnapshot) {
        for (const char* game : { "oot", "mm" }) {
            const auto current = mSnapshot(game);
            if (current.is_object()) {
                for (const auto& [key, value] : current.at("settings").items()) {
                    mCatalogue[game]["settings"][key] = value;
                }
            }
        }
    }
    for (const char* game : { "oot", "mm" }) {
        mCatalogue[game]["settings"]["fleet.startingItems"] = mFleetStarting.at(game);
    }
    nlohmann::json settings = mCatalogue;
    settings["fleet"] = FleetBlock();
    return settings;
}

nlohmann::json FleetSaveMenu::FleetBlock() const {
    const nlohmann::json goal = { { "type", mHunt ? "triforceHunt" : "bosses" },
                                  { "mode", mEitherGame ? "either" : "both" },
                                  { "required", mRequired },
                                  { "total", mTotal },
                                  { "pieces", mHunt ? 0 : mBossGoalPieces } };
    return { { "goal", goal },
             { "songs", kSongPlacementKeys[mSongs] },
             { "ownGame", OwnGameKeys(mOwnGame) },
             { "keyRingCount", mKeyRingCount },
             { "rewards", kRewardPlacementKeys[mRewards] },
             { "plando", PlandoJson() },
             { "poolEdits", PoolEditsJson() } };
}

std::vector<std::string> FleetSaveMenu::FleetArguments(const std::string& seed) {
    const char* starts[] = { "oot", "mm", "random" };
    std::vector<std::string> args = { "--seed", seed, "--start", starts[mStart] };
    args.insert(args.end(), { "--plentiful-mod-items", mPlentifulModItems ? "true" : "false" });
    std::string shared;
    for (const std::string& key : mShareChoices.SharedOptionKeys()) {
        shared += (shared.empty() ? "" : ",") + key;
    }
    if (!shared.empty()) {
        args.insert(args.end(), { "--shared", shared });
    }
    std::string unshared;
    for (const auto& key : mShareChoices.UnsharedPairs()) {
        unshared += (unshared.empty() ? "" : ",") + key;
    }
    if (!unshared.empty()) {
        args.insert(args.end(), { "--unshared", unshared });
    }
    std::string pools;
    for (size_t i = 0; i < mPools.size(); i++) {
        if (mPools[i] && !IsCrossPoolLocked(i, mPools)) {
            pools += (pools.empty() ? "" : ",") + std::string(kCrossPools[i].key);
        }
    }
    if (!pools.empty()) {
        args.insert(args.end(), { "--cross", pools });
    }
    if (const std::string passages = PassageArgument(mPassages); !passages.empty()) {
        args.insert(args.end(), { "--passages", passages });
    }
    if (mHunt) {
        args.insert(args.end(), { "--triforce", std::to_string(mRequired) + ":" + std::to_string(mTotal) });
    }
    return args;
}

std::vector<std::string> FleetSaveMenu::GenerationArguments(const std::filesystem::path& run, const std::string& seed) {
    std::filesystem::create_directories(run);
    Write(run / "settings.json", ComposeSettings());
    auto args = BaseArguments();
    args.insert(args.end(), { "--settings", (run / "settings.json").string() });
    const auto flags = FleetArguments(seed);
    args.insert(args.end(), flags.begin(), flags.end());
    return args;
}

void FleetSaveMenu::KeepFile(const std::string& name, const nlohmann::json& json) {
    try {
        Write(mRoot / name, json);
    } catch (const std::exception& error) { mError = error.what(); }
}

void FleetSaveMenu::SetIconSource(IconSource source) {
    mIconSource = std::move(source);
}

void FleetSaveMenu::SetGameRequest(GameRequest request) {
    mGameRequest = std::move(request);
}

void FleetSaveMenu::PrepareWorkerPage(WorkerFeed& feed) {
    try {
        LoadCatalogue();
        if (mStartingStale) {
            LoadStartingItems();
        }
        FeedWorker(feed);
    } catch (const std::exception& error) { feed.error = error.what(); }
}

void FleetSaveMenu::FeedWorker(WorkerFeed& feed) {
    const auto now = std::chrono::steady_clock::now();
    if (!mCatalogue.is_object() || now - feed.checkedAt < kSettleTime) {
        return;
    }
    feed.checkedAt = now;
    PollWorker();
    auto settings = ComposeSettings();
    for (const auto& key : feed.ignoredKeys) {
        settings["fleet"].erase(key);
    }
    const auto arguments = FleetArguments(mSeed[0] != '\0' ? std::string(mSeed.data()) : "1");
    std::string fingerprint = settings.dump();
    for (const auto& argument : arguments) {
        fingerprint += '\n' + argument;
    }
    if (fingerprint == feed.requested) {
        return;
    }
    const bool settled = fingerprint == feed.observed;
    feed.observed = fingerprint;
    if (!settled || !mWorkerOp.empty() || (mPoolWorker && mPoolWorker->IsBusy())) {
        return;
    }
    WarmWorker();
    const auto run = mRoot / feed.op;
    std::filesystem::create_directories(run);
    Write(run / "settings.json", settings);
    mPoolWorker->Request(feed.op, run / "settings.json", arguments);
    feed.requested = fingerprint;
    mWorkerOp = feed.op;
    if (feed.op == mPoolFeed.op) {
        mStartingRequested = mStarting;
        mFleetStartingRequested = mFleetStarting;
    }
}

bool FleetSaveMenu::IsComputing(const WorkerFeed& feed) const {
    return mWorkerOp == feed.op || feed.observed != feed.requested;
}

void FleetSaveMenu::DrawWorkerStatus(const WorkerFeed& feed, const char* idle) {
    if (!mCatalogue.is_object()) {
        ImGui::TextDisabled("Loading randomizer options...");
    } else if (IsComputing(feed)) {
        const bool warming = mPoolWorker && !mPoolWorker->IsWarm();
        ImGui::TextDisabled(warming ? "Starting both games in the background..." : "Computing...");
    } else {
        ImGui::TextDisabled("%s", idle);
    }
    if (!feed.error.empty()) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", feed.error.c_str());
    }
}

void FleetSaveMenu::PollWorker() {
    if (!mPoolWorker) {
        return;
    }
    const auto reply = mPoolWorker->TakeReply();
    if (!reply) {
        if (!mPoolWorker->IsBusy() && !mWorkerOp.empty()) {
            mWorkerOp.clear();
            mPoolFeed.requested.clear();
            mChecksFeed.requested.clear();
        }
        return;
    }
    const std::string op = std::exchange(mWorkerOp, std::string{});
    if (op == mChecksFeed.op) {
        ReceiveChecks(*reply);
    } else {
        ReceivePool(*reply);
    }
}

// Booting both games with every mod takes seconds, so it starts on any randomizer page, not on the pool page.
void FleetSaveMenu::WarmPoolWorker() {
    if (!mActive || mReady) {
        return;
    }
    WarmWorker();
}

void FleetSaveMenu::WarmWorker() {
    if (mPoolWorker) {
        return;
    }
    std::filesystem::create_directories(mRoot / "worker");
    mPoolWorker = std::make_unique<PoolWorker>(mDirectory / kFleetExecutable, BaseArguments(), mRoot / "worker");
}

void FleetSaveMenu::StopWorker() {
    if (!mPoolWorker) {
        return;
    }
    mPoolWorker->Stop();
    mPoolWorker.reset();
    mWorkerOp.clear();
    mPoolFeed.requested.clear();
    mChecksFeed.requested.clear();
}

void FleetSaveMenu::RememberNames(const nlohmann::json& items) {
    for (const auto& item : items) {
        const ItemKey key = { item.value("game", std::string{}), item.value("name", std::string{}) };
        if (item.contains("displayName")) {
            mDisplayNames[key] = item.value("displayName", std::string{});
        }
        if (item.contains("description")) {
            mDescriptions[key] = item.value("description", std::string{});
        }
    }
}

void FleetSaveMenu::RememberPoolCoverage(const nlohmann::json& pairs) {
    mCoveredByPool.clear();
    mPoolNamesByGame.clear();
    mSharedInPool.clear();
    for (const auto& item : mPool) {
        if (item.value("shared", false)) {
            mSharedInPool.insert({ item.value("game", std::string{}), item.value("name", std::string{}) });
        }
    }
    for (const auto& [key, copies] : mPoolCopies) {
        mCoveredByPool.insert(key);
        mPoolNamesByGame[key.first].insert(Lowercase(DisplayName(key.first, key.second)));
    }
    mTwins.clear();
    const auto coverTwins = [this](const ItemKey& oot, const ItemKey& mm) {
        mTwins[oot] = mm.second;
        mTwins[mm] = oot.second;
        if (mPoolCopies.contains(oot) || mPoolCopies.contains(mm)) {
            mCoveredByPool.insert(oot);
            mCoveredByPool.insert(mm);
        }
    };
    for (const auto& pair : pairs) {
        coverTwins({ "oot", pair.value("oot", std::string{}) }, { "mm", pair.value("mm", std::string{}) });
    }
    for (int family = 0; family < ComboRando::SF_COUNT; family++) {
        const auto& definition = ComboRando::SharedFamilyByIndex(family);
        if (definition.mmHasItem) {
            coverTwins({ "oot", definition.ootName }, { "mm", definition.mmName });
        }
    }
}

bool FleetSaveMenu::IsCoveredByPool(const std::string& game, const std::string& name) const {
    if (mCoveredByPool.contains({ game, name })) {
        return true;
    }
    const auto other = mPoolNamesByGame.find(game == "oot" ? "mm" : "oot");
    return other != mPoolNamesByGame.end() && other->second.contains(Lowercase(DisplayName(game, name)));
}

void FleetSaveMenu::ReceivePool(const nlohmann::json& reply) {
    mPoolFeed.error = reply.is_object() ? reply.value("error", std::string{}) : std::string{};
    if (!reply.is_object() || !reply.value("items", nlohmann::json()).is_array()) {
        if (mPoolFeed.error.empty()) {
            mPoolFeed.error = "The pool could not be computed with these options.";
        }
        return;
    }
    mPool = reply.at("items");
    FoldTierAliases(mPool);
    std::vector<ItemKey> joinedIntoChains = GroupUpgradeChains();
    const std::vector<ItemKey> joinedIntoVariants = GroupVariants();
    joinedIntoChains.insert(joinedIntoChains.end(), joinedIntoVariants.begin(), joinedIntoVariants.end());
    if (reply.contains("pairs")) {
        RebuildShareRows(reply.at("pairs"));
        mShareChoices.LearnPairRows(PairRowKeys());
        KeepShareChoices();
    }
    mStartingAtPool = mStartingRequested;
    mFleetStartingAtPool = mFleetStartingRequested;
    mPoolCopies.clear();
    for (const auto& item : mPool) {
        mPoolCopies[{ item.value("game", std::string{}), item.value("name", std::string{}) }] += PoolCopies(item);
    }
    RememberNames(mPool);
    RememberPoolCoverage(reply.value("pairs", nlohmann::json::array()));
    mCoveredByPool.insert(joinedIntoChains.begin(), joinedIntoChains.end());
    SortPool();
}

std::vector<FleetSaveMenu::ItemKey> FleetSaveMenu::GroupUpgradeChains() {
    mUpgradeTiers.clear();
    std::vector<ItemKey> joinedKeys;
    const auto oot = GameVariables("oot");
    const auto whereFrom = [](const nlohmann::json& item) {
        return std::string(item.value("shared", false) ? "Shared" : GameLabel(item.value("game", std::string{})));
    };
    for (const UpgradeChain& chain : kUpgradeChains) {
        const auto kept = FindPoolItem(mPool, "oot", chain.progressive);
        if (kept == mPool.end()) {
            continue;
        }
        const size_t keptIndex = static_cast<size_t>(std::distance(mPool.begin(), kept));
        std::vector<size_t> joined;
        std::vector<std::string> lines;
        const auto joinItems = [&](const std::vector<const char*>& names) {
            for (const char* shown : names) {
                for (size_t index = 0; index < mPool.size(); index++) {
                    if (index != keptIndex && mPool[index].value("displayName", std::string{}) == shown) {
                        lines.push_back(std::string(shown) + " (" + whereFrom(mPool[index]) + ")");
                        joined.push_back(index);
                    }
                }
            }
        };
        joinItems(chain.firsts);
        std::vector<std::string> pooledTiers;
        for (const UpgradeTier& tier : chain.tiers) {
            if (tier.onlyWith == nullptr || (oot && oot->GetInteger(tier.onlyWith, 0) == 1)) {
                pooledTiers.push_back(tier.name);
            }
        }
        const size_t copies = static_cast<size_t>(PoolCopies(mPool[keptIndex]));
        const size_t firstPooled = pooledTiers.size() > copies ? pooledTiers.size() - copies : 0;
        const std::string keptFrom = whereFrom(mPool[keptIndex]);
        for (size_t tier = firstPooled; tier < firstPooled + copies; tier++) {
            const std::string name = tier < pooledTiers.size() ? pooledTiers[tier] : "Extra tier from a mod";
            lines.push_back(name + " (" + keptFrom + ")");
        }
        joinItems(chain.lasts);
        nlohmann::json& keptItem = mPool[keptIndex];
        for (size_t index : joined) {
            keptItem["shuffled"] = keptItem.value("shuffled", 0) + mPool[index].value("shuffled", 0);
            keptItem["fixed"] = keptItem.value("fixed", 0) + mPool[index].value("fixed", 0);
            joinedKeys.push_back(
                { mPool[index].value("game", std::string{}), mPool[index].value("name", std::string{}) });
        }
        mUpgradeTiers[{ "oot", chain.progressive }] = std::move(lines);
        std::sort(joined.rbegin(), joined.rend());
        for (size_t index : joined) {
            mPool.erase(mPool.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }
    return joinedKeys;
}

std::vector<FleetSaveMenu::ItemKey> FleetSaveMenu::GroupVariants() {
    mVariants.clear();
    std::vector<ItemKey> joinedKeys;
    std::map<ItemKey, size_t> keptByOwner;
    std::vector<size_t> joined;
    for (size_t index = 0; index < mPool.size(); index++) {
        const nlohmann::json& item = mPool[index];
        const std::string owner = item.value("variantOf", std::string{});
        if (owner.empty()) {
            continue;
        }
        const std::string game = item.value("game", std::string{});
        const std::string shown = item.value("displayName", item.value("name", std::string{}));
        const auto [kept, isFirst] = keptByOwner.try_emplace({ game, owner }, index);
        nlohmann::json& keptItem = mPool[kept->second];
        const ItemKey keptKey = { game, keptItem.value("name", std::string{}) };
        mVariants[keptKey].push_back(shown + " x" + std::to_string(PoolCopies(item)));
        if (isFirst) {
            mDisplayNames[keptKey] = item.value("variantOfName", owner);
            keptItem["displayName"] = mDisplayNames[keptKey];
            continue;
        }
        keptItem["shuffled"] = keptItem.value("shuffled", 0) + item.value("shuffled", 0);
        keptItem["fixed"] = keptItem.value("fixed", 0) + item.value("fixed", 0);
        joinedKeys.push_back({ game, item.value("name", std::string{}) });
        joined.push_back(index);
    }
    std::sort(joined.rbegin(), joined.rend());
    for (size_t index : joined) {
        mPool.erase(mPool.begin() + static_cast<std::ptrdiff_t>(index));
    }
    return joinedKeys;
}

void FleetSaveMenu::SortPool() {
    struct RankedItem {
        size_t rank;
        std::string shown;
        nlohmann::json item;
    };
    std::vector<RankedItem> ranked;
    for (auto& item : mPool) {
        std::string shown =
            Lowercase(DisplayName(item.value("game", std::string{}), item.value("name", std::string{})));
        const size_t rank = PoolSortRank(ClassifyPoolItem(item), shown);
        ranked.push_back({ rank, std::move(shown), std::move(item) });
    }
    std::sort(ranked.begin(), ranked.end(), [](const RankedItem& left, const RankedItem& right) {
        return std::tie(left.rank, left.shown) < std::tie(right.rank, right.shown);
    });
    mPool = nlohmann::json::array();
    for (auto& entry : ranked) {
        mPool.push_back(std::move(entry.item));
    }
}

void FleetSaveMenu::ReceiveChecks(const nlohmann::json& reply) {
    mChecksFeed.error = reply.is_object() ? reply.value("error", std::string{}) : std::string{};
    if (!reply.is_object() || !reply.contains("oot")) {
        if (mChecksFeed.error.empty()) {
            mChecksFeed.error = "The checks could not be listed with these options.";
        }
        return;
    }
    const std::string chosenArea = mAreaFilter > 0 ? mAreas[mAreaFilter - 1] : std::string{};
    std::set<std::string> areas;
    mChecks.clear();
    for (const char* game : { "oot", "mm" }) {
        for (const auto& check : reply.value(game, nlohmann::json::array())) {
            mChecks.push_back({ game, check.value("name", ""), check.value("area", ""), check.value("vanilla", "") });
            areas.insert(mChecks.back().area);
        }
    }
    mAreas.assign(areas.begin(), areas.end());
    const auto kept = std::find(mAreas.begin(), mAreas.end(), chosenArea);
    mAreaFilter = kept == mAreas.end() ? 0 : static_cast<int>(kept - mAreas.begin()) + 1;
    const auto items = reply.value("items", nlohmann::json::array());
    RememberNames(items);
    mPlandoItems.clear();
    for (const auto& item : items) {
        const std::string game = item.value("game", std::string{});
        const std::string name = item.value("name", std::string{});
        mPlandoItems.push_back({ game, name, DisplayName(game, name) + " (" + GameLabel(game) + ")" });
    }
    std::sort(mPlandoItems.begin(), mPlandoItems.end(), [](const PlandoItem& left, const PlandoItem& right) {
        return Lowercase(left.label) < Lowercase(right.label);
    });
    mChecksVersion++;
}

void FleetSaveMenu::Poll() {
    PollWorker();
    if (const auto exported = mArchipelagoJob.Poll()) {
        const std::filesystem::path folder = ArchipelagoFolder(mDirectory);
        if (*exported == 0) {
            mArchipelagoStatus = "To play, choose Archipelago when Fleet starts.";
            mArchipelagoActions.Offer(folder, mArchipelagoSlot.data());
        } else {
            mArchipelagoStatus = "The export failed; see the archipelago.log of this run.";
        }
    }
    try {
        if (const auto result = mJob.Poll()) {
            if (mLoadingCatalogue) {
                mLoadingCatalogue = false;
                if (*result != 0) {
                    throw std::runtime_error("Could not load the game options. Restart Fleet to try again.");
                }
                mCatalogue = nlohmann::json::parse(std::ifstream(mRoot / "options.json"));
                for (const char* game : { "oot", "mm" }) {
                    if (!mCatalogue.at(game).at("settings").is_object() ||
                        !mCatalogue.at(game).at("definitions").is_array()) {
                        throw std::runtime_error("The games did not provide their randomizer options.");
                    }
                }
            } else if (mGenerating) {
                mGenerating = false;
                if (*result != 0) {
                    throw std::runtime_error("No valid seed was found with these options. Change the seed number or "
                                             "settings and try again.");
                }
                const auto seed = nlohmann::json::parse(std::ifstream(mRun / "seed.json"));
                if (seed.value("fileType", "") != "ComboShipRandomizer") {
                    throw std::runtime_error("The generated seed is invalid. No save has been created.");
                }
                mReady = Ready{ mRun / "seed.json", mRun / "session", mSlot, true };
                mResult = seed;
                try {
                    StoreSeedFile(mRun / "seed.json", "Fleet-" + std::string(mSeed.data()) + ".json");
                    RefreshSeedFiles();
                } catch (const std::exception& error) {
                    std::cerr << "[fleet] could not keep the seed in the Randomizer folder: " << error.what()
                              << std::endl;
                }
                mStartRequested = mAutomated;
            }
        }
        if (mNativeRequested && !mJob.Running() && mCatalogue.is_object()) {
            mNativeRequested = false;
            Generate();
        }
        GenerateOncePoolArrives();
        if (mAutomation >= 0 && mPrepared && !mReady && mSlot < 0) {
            SelectNative(mAutomation);
        }
        if (mAutomation >= 0 && mActive && mDrawn && !mJob.Running() && !mReady && mCatalogue.is_object()) {
            const int slot = mAutomation;
            mAutomation = -1;
            if (mResumeTest && !mSlots.contains(std::to_string(slot))) {
                throw std::runtime_error("The test save does not exist.");
            }
            Select(slot);
            if (!mReady) {
                std::fill(mSeed.begin(), mSeed.end(), 0);
                const std::string seed = "80302";
                std::copy(seed.begin(), seed.end(), mSeed.begin());
                Generate();
            }
        }
    } catch (const std::exception& error) {
        Fail(error.what());
        std::cerr << "[fleet] file select: " << error.what() << std::endl;
    }
}

void FleetSaveMenu::Automate(int slot, bool resume, bool directStart) {
    mAutomated = directStart;
    mAutomation = slot;
    mResumeTest = resume;
}

std::optional<FleetSaveMenu::Ready> FleetSaveMenu::Pending() const {
    return !mActive && mStartRequested ? mReady : std::nullopt;
}

std::optional<FleetSaveMenu::Ready> FleetSaveMenu::Generated(int slot) const {
    return mReady && mReady->fresh && mReady->slot == slot ? mReady : std::nullopt;
}

void FleetSaveMenu::Complete() {
    if (mReady->fresh) {
        mSlots[std::to_string(mReady->slot)] = { { "seed", mReady->seed.string() },
                                                 { "session", mReady->session.string() },
                                                 { "number", std::string(mSeed.data()) } };
        Write(mRoot / "slots.json", mSlots);
    }
    mReady.reset();
    mEnabled = mActive = false;
    StopWorker();
}

void FleetSaveMenu::Reopen() {
    mReady.reset();
    mSlot = -1;
    mStartRequested = false;
    mActive = false;
    mNativeMode = false;
    mNativeRequested = false;
    mPrepared = false;
    mEnabled = true;
}

void FleetSaveMenu::Draw() {
    if (mActive) {
        if (mReady && mStartRequested) {
            mActive = false;
        }
    }
}

bool FleetSaveMenu::CloseConfiguration() {
    if (mGenerating) {
        return false;
    }
    if (mActive) {
        if (mReady) {
            mActive = false;
            return true;
        }
        mJob.Cancel();
        mLoadingCatalogue = false;
        mGenerateWhenPoolArrives = false;
        mActive = false;
        mSlot = -1;
    }
    return true;
}

void FleetSaveMenu::DrawElement() {
    DrawGeneralPage();
}

void FleetSaveMenu::DrawGeneralPage() {
    if (!mDrawn) {
        std::cout << "[fleet] generation panel drawing" << std::endl;
    }
    mDrawn = true;
    if (!mError.empty()) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Error: %s", mError.c_str());
    }
    if (mGenerating || mLoadingCatalogue || mGenerateWhenPoolArrives) {
        DrawGenerationProgress();
        return;
    }
    const auto theme = ComboRando::ComboMenu_ThemeColor();
    ImGui::TextWrapped(
        "Generate a cross-world randomizer seed spanning OOT and MM. You must Generate before starting a new file.");
    ImGui::SeparatorText("Seed");
    DrawSeedChoice(theme);
    DrawGenerationResult();
    ImGui::SeparatorText("Starting Game");
    DrawStartingGame(theme);
    ImGui::SeparatorText("Goal");
    DrawGoal(theme);
    KeepGeneralChoices();
}

void FleetSaveMenu::DrawCrossEntranceRows() {
    DrawCrossEntrances(ComboRando::ComboMenu_ThemeColor());
    KeepGeneralChoices();
}

void FleetSaveMenu::DrawBossEntranceRow() {
    const auto boss = std::find_if(kEntranceRows.begin(), kEntranceRows.end(), [](const EntranceRow& row) {
        return std::string_view(row.cvar != nullptr ? row.cvar : "") == "gRandoSettings.ShuffleBossEntrances";
    });
    if (boss == kEntranceRows.end()) {
        return;
    }
    DrawEntranceRow(*boss);
    KeepGeneralChoices();
}

bool FleetSaveMenu::IsMmStartingWith(const char* name) const {
    return StartingCount("mm", name) > 0;
}

void FleetSaveMenu::ApplyEquipmentDefaults() {
    const auto oot = GameVariables("oot");
    if (mEquipmentDefaultsApplied || !oot || !mGameRequest) {
        return;
    }
    mEquipmentDefaultsApplied = true;
    oot->SetInteger(kShuffleKokiriSwordCvar, 1);
    oot->Save();
    SetStartingCount("mm", kMmSwordName, 0);
    SetStartingCount("mm", kMmShieldName, 0);
    mStartingStale = true;
}

void FleetSaveMenu::DrawStartingEquipment() {
    if (mStartingStale) {
        LoadStartingItems();
    }
    ApplyEquipmentDefaults();
    const auto oot = GameVariables("oot");
    if (!oot) {
        return;
    }
    ComboRando::ComboMenu_PushCheckbox(ComboRando::ComboMenu_ThemeColor());
    bool isKokiriShuffled = oot->GetInteger(kShuffleKokiriSwordCvar, 0) != 0 && !IsMmStartingWith(kMmSwordName);
    if (ImGui::Checkbox("Shuffle Kokiri Sword", &isKokiriShuffled)) {
        oot->SetInteger(kShuffleKokiriSwordCvar, isKokiriShuffled ? 1 : 0);
        oot->Save();
        SetStartingCount("mm", kMmSwordName, isKokiriShuffled ? 0 : 1);
        mStartingStale = true;
    }
    ImGui::SetItemTooltip(
        "Ocarina of Time: the Kokiri Sword leaves its chest for the pool.\nMajora's Mask: Link starts "
        "without a sword and finds his first one in the pool.");
    DrawGameCheckbox("Shuffle Master Sword", "oot", "gRandoSettings.ShuffleMasterSword");
    ImGui::SetItemTooltip("Ocarina of Time only: the Master Sword leaves its pedestal for the pool.");
    bool isShieldShuffled = !IsMmStartingWith(kMmShieldName);
    if (ImGui::Checkbox("Shuffle Hylian / Hero's Shield", &isShieldShuffled)) {
        SetStartingCount("mm", kMmShieldName, isShieldShuffled ? 0 : 1);
        mStartingStale = true;
    }
    ImGui::SetItemTooltip("Majora's Mask: Link starts without the Hero's Shield and finds it in the pool.\nOcarina of "
                          "Time's Hylian Shield is always found in the world.");
    ComboRando::ComboMenu_PopCheckbox();
    KeepGeneralChoices();
}

void FleetSaveMenu::DrawKeyRingCount() {
    const auto oot = GameVariables("oot");
    if (!oot || oot->GetInteger("gRandoSettings.ShuffleKeyRings", 0) != kKeyRingModeCount) {
        return;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ComboRando::ComboMenu_PushSlider(ComboRando::ComboMenu_ThemeColor());
    if (ImGui::SliderInt("##fleetKeyRingCount", &mKeyRingCount, 0, kKeyRingDungeons, "Key Rings (both games): %d")) {
        KeepLogicChoices();
    }
    ComboRando::ComboMenu_PopSlider();
    ImGui::SetItemTooltip("How many dungeons of both games get a key ring, drawn from Ocarina of Time's and Majora's "
                          "Mask's together.");
}

void FleetSaveMenu::DrawGenerationProgress() {
    if (mGenerating) {
        DrawSeedProgress();
    } else if (mGenerateWhenPoolArrives) {
        ImGui::TextWrapped("Computing the item pool so Share/Unshare All covers the mod items...");
    } else {
        ImGui::TextWrapped("Loading randomizer options...");
    }
    if (mGenerating && ImGui::Button("Cancel generation")) {
        mJob.Cancel();
        mGenerating = false;
        mError.clear();
    }
}

const nlohmann::json& FleetSaveMenu::ReadSeedProgress() {
    constexpr auto kReadInterval = std::chrono::milliseconds(250);
    const auto now = std::chrono::steady_clock::now();
    if (now - mSeedProgressRead < kReadInterval) {
        return mSeedProgress;
    }
    mSeedProgressRead = now;
    const auto status = nlohmann::json::parse(std::ifstream(mRun / "progress.json"), nullptr, false);
    if (status.is_object()) {
        mSeedProgress = status;
    }
    return mSeedProgress;
}

static std::string FormatDuration(double seconds) {
    const int total = static_cast<int>(seconds + 0.5);
    char text[16];
    std::snprintf(text, sizeof(text), "%d:%02d", total / 60, total % 60);
    return text;
}

void FleetSaveMenu::DrawSeedProgress() {
    constexpr float kEtaMinimumFraction = 0.1f;
    const nlohmann::json& status = ReadSeedProgress();
    const float fraction = status.value("fraction", 0.0f);
    const std::string title = status.value("title", std::string("Starting both games"));
    ImGui::Text("Step %d of %d: %s", status.value("stage", 1), status.value("stages", 6), title.c_str());
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ComboRando::ComboMenu_ThemeColor());
    ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0));
    ImGui::PopStyleColor();
    ImGui::TextWrapped("%s", status.value("detail", std::string("Loading Ocarina of Time, Majora's Mask and every "
                                                                "installed mod."))
                                 .c_str());
    const std::string count = status.value("count", std::string{});
    if (!count.empty()) {
        ImGui::TextWrapped("%s", count.c_str());
    }
    const int attempt = status.value("attempt", 1);
    if (attempt > 1) {
        ImGui::TextWrapped("Attempt %d: the previous layout could not be completed, so a new one is being tried.",
                           attempt);
    }
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - mGenerationStart).count();
    if (fraction >= kEtaMinimumFraction) {
        const double remaining = elapsed * (1.0 - fraction) / fraction;
        ImGui::Text("Elapsed %s, about %s left", FormatDuration(elapsed).c_str(), FormatDuration(remaining).c_str());
    } else {
        ImGui::Text("Elapsed %s", FormatDuration(elapsed).c_str());
    }
    ImGui::TextDisabled("Measured on the test seeds: about 1 minute when only zones are shuffled, 5 to 7 minutes "
                        "with dungeons, bosses and entrances crossed. Finding required items takes most of it.");
}

void FleetSaveMenu::DrawGenerationResult() {
    if (!mReady || !mReady->fresh || !mResult.is_object()) {
        return;
    }
    ImGui::Text("Seed: %s", mSeed.data());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        ImGui::SetClipboardText(mSeed.data());
    }
    ImGui::Text("OOT checks: %zu", mResult.at("oot").at("placements").size());
    ImGui::Text("MM checks: %zu", mResult.at("mm").at("placements").size());
    ImGui::Text("Cross-game placements: %zu", mResult.at("foreign").size());
    ImGui::TextDisabled("Close settings, then choose Start Randomizer and enter your name.");
}

void FleetSaveMenu::DrawStartingGame(const ImVec4& theme) {
    ImGui::SetNextItemWidth(260);
    ComboRando::ComboMenu_PushCombobox(theme);
    ImGui::Combo("##startinggame", &mStart, "Ocarina of Time\0Majora's Mask\0Random\0");
    ComboRando::ComboMenu_PopCombobox();
    ImGui::TextDisabled("Majora's Mask starts the file in South Clock Town. Ocarina of Time reaches Termina through "
                        "the Happy Mask Shop, carrying an ocarina and the Song of Time.");
}

void FleetSaveMenu::DrawGoal(const ImVec4& theme) {
    int type = mHunt ? 1 : 0;
    ImGui::SetNextItemWidth(260);
    ComboRando::ComboMenu_PushCombobox(theme);
    if (ImGui::Combo("Goal Type", &type, kGoalTypes.data(), static_cast<int>(kGoalTypes.size()))) {
        mHunt = type == 1;
    }
    ComboRando::ComboMenu_PopCombobox();
    if (mHunt) {
        ComboRando::ComboMenu_PushInput(theme);
        ImGui::SetNextItemWidth(260);
        ImGui::InputInt("Required pieces (both games)", &mRequired);
        mRequired = std::clamp(mRequired, 1, ComboRando::kMaxComboTriforcePieces);
        ImGui::SetNextItemWidth(260);
        ImGui::InputInt("Pieces in the pool (both games)", &mTotal);
        mTotal = std::clamp(mTotal, mRequired, ComboRando::kMaxComboTriforcePieces);
        ComboRando::ComboMenu_PopInput();
        ComboRando::ComboMenu_PushCheckbox(theme);
        if (ImGui::Checkbox("Pieces stay in their own game", &mOwnGame[OwnGameIndex("triforce")])) {
            KeepLogicChoices();
        }
        ComboRando::ComboMenu_PopCheckbox();
        ImGui::TextDisabled("Collect this many pieces across OOT and MM to finish; bosses are optional.");
        return;
    }
    int mode = mEitherGame ? 1 : 0;
    ImGui::SetNextItemWidth(260);
    ComboRando::ComboMenu_PushCombobox(theme);
    if (ImGui::Combo("Goal Mode", &mode, kGoalModes.data(), static_cast<int>(kGoalModes.size()))) {
        mEitherGame = mode == 1;
    }
    ComboRando::ComboMenu_PopCombobox();
    ImGui::TextDisabled(mEitherGame ? "Beat Ganon or Majora to finish."
                                    : "Beat Ganon and Majora to finish; the first one only records its game.");
    ComboRando::ComboMenu_PushInput(theme);
    ImGui::SetNextItemWidth(260);
    ImGui::InputInt("Triforce pieces in the pool (both games)", &mBossGoalPieces);
    mBossGoalPieces = std::clamp(mBossGoalPieces, 0, ComboRando::kMaxComboTriforcePieces);
    ComboRando::ComboMenu_PopInput();
    ImGui::SetItemTooltip("Pieces found in either game count for both. Use them as triggers: Ganon's Boss Key, Ganon's "
                          "Soul, the Rainbow Bridge, Majora's Soul or the Moon.");
}

void FleetSaveMenu::DrawCrossEntrances(const ImVec4& theme) {
    if (mPools[kPoolInteriors]) {
        mPools[kPoolDoorInteriors] = false;
    }
    for (const EntranceRow& row : kEntranceRows) {
        DrawEntranceRow(row);
    }
    if (mPools[kPoolDungeons]) {
        ComboRando::ComboMenu_PushCheckbox(theme);
        for (const size_t index : { kPoolExpanded, kPoolDecoupled }) {
            ImGui::Checkbox(kCrossPools[index].label, &mPools[index]);
        }
        ImGui::SetItemTooltip("A dungeon's way out no longer leads back outside the door that led in.");
        ComboRando::ComboMenu_PopCheckbox();
    }
    DrawPassages(mPassages, theme);
}

bool DrawLabeledCombo(const char* id, const char* label, int& value, std::span<const char* const> choices) {
    float widest = 0;
    for (const char* choice : choices) {
        widest = std::max(widest, ImGui::CalcTextSize(choice).x);
    }
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(widest + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 4);
    ComboRando::ComboMenu_PushCombobox(ComboRando::ComboMenu_ThemeColor());
    bool changed = false;
    if (ImGui::BeginCombo(id, choices[value])) {
        for (int choice = 0; choice < static_cast<int>(choices.size()); choice++) {
            if (ImGui::Selectable(choices[choice], choice == value) && choice != value) {
                value = choice;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ComboRando::ComboMenu_PopCombobox();
    return changed;
}

void FleetSaveMenu::DrawEntranceRow(const EntranceRow& row) {
    const auto oot = GameVariables("oot");
    if (!oot) {
        return;
    }
    std::set<size_t> rowPools;
    std::vector<const char*> labels;
    for (const auto& choice : row.choices) {
        rowPools.insert(choice.pools.begin(), choice.pools.end());
        labels.push_back(choice.label);
    }
    const int stored = row.cvar != nullptr ? oot->GetInteger(row.cvar, 0) : 0;
    const auto isChosen = [&](const EntranceChoice& choice) {
        return choice.native == stored && std::all_of(rowPools.begin(), rowPools.end(), [&](size_t pool) {
                   const bool isInChoice =
                       std::find(choice.pools.begin(), choice.pools.end(), pool) != choice.pools.end();
                   return mPools[pool] == isInChoice;
               });
    };
    const auto found = std::find_if(row.choices.begin(), row.choices.end(), isChosen);
    int shown = found == row.choices.end() ? 0 : static_cast<int>(found - row.choices.begin());
    ImGui::PushID(row.label);
    if (DrawLabeledCombo("##entrances", row.label, shown, labels)) {
        const EntranceChoice& choice = row.choices[shown];
        if (row.cvar != nullptr) {
            oot->SetInteger(row.cvar, choice.native);
            oot->Save();
        }
        for (const size_t pool : rowPools) {
            mPools[pool] = std::find(choice.pools.begin(), choice.pools.end(), pool) != choice.pools.end();
        }
    }
    ImGui::SetItemTooltip("%s", row.tooltip);
    ImGui::PopID();
}

void FleetSaveMenu::DrawSongPlacement() {
    if (DrawLabeledCombo("##fleetSongs", "Song Placement", mSongs, kSongPlacementLabels)) {
        KeepLogicChoices();
    }
    ImGui::SetItemTooltip("Where the songs of both games and mods may land. Native keeps each game's own song rule; "
                          "a song that finds no song check left lands anywhere.");
}

void FleetSaveMenu::DrawDungeonItemRow(const DungeonItemRow& row) {
    const auto variables = GameVariables(row.game);
    if (!variables) {
        return;
    }
    std::vector<const char*> labels;
    for (const auto& choice : row.choices) {
        labels.push_back(choice.label);
    }
    const size_t ownGame = OwnGameIndex(row.ownGameKey);
    const bool isRewardsRow = std::string_view(row.ownGameKey) == "dungeonReward";
    const std::string rewards = isRewardsRow ? kRewardPlacementKeys[mRewards] : "native";
    int stored = variables->GetInteger(row.cvar, row.defaultValue);
    if (isRewardsRow && rewards == "native" && stored == kOotEndOfDungeons) {
        stored = row.anywhereValue;
        mRewards = kRewardsBossesThisGame;
    }
    const auto isShown = [&](const PlacementChoice& choice) {
        const std::string choiceRewards = choice.rewards;
        return choice.value == stored && choice.isOwnGame == mOwnGame[ownGame] &&
               (!isRewardsRow || choiceRewards == kRewardPlacementKeys[mRewards]);
    };
    const auto found = std::find_if(row.choices.begin(), row.choices.end(), isShown);
    int shown = found == row.choices.end() ? 0 : static_cast<int>(found - row.choices.begin());
    ImGui::PushID(row.cvar);
    if (DrawLabeledCombo("##placement", row.label, shown, labels)) {
        const PlacementChoice& choice = row.choices[shown];
        variables->SetInteger(row.cvar, choice.value);
        variables->Save();
        mOwnGame[ownGame] = choice.isOwnGame;
        if (isRewardsRow) {
            const auto key =
                std::find(kRewardPlacementKeys.begin(), kRewardPlacementKeys.end(), std::string_view(choice.rewards));
            mRewards = static_cast<int>(key - kRewardPlacementKeys.begin());
        }
        KeepLogicChoices();
    }
    ImGui::SetItemTooltip("%s%s", row.tooltip, kPlacementHelp);
    ImGui::PopID();
}

void FleetSaveMenu::DrawDungeonItemRows() {
    for (const DungeonItemRow& row : kDungeonItemRows) {
        DrawDungeonItemRow(row);
    }
}

void FleetSaveMenu::LoadLogicChoices() {
    const auto stored = nlohmann::json::parse(std::ifstream(mRoot / "logic.json"), nullptr, false);
    if (!stored.is_object()) {
        return;
    }
    const std::string songs = stored.value("songs", std::string{});
    const auto song = std::find(kSongPlacementKeys.begin(), kSongPlacementKeys.end(), songs);
    mSongs = song == kSongPlacementKeys.end() ? 0 : static_cast<int>(song - kSongPlacementKeys.begin());
    const auto ownGame = stored.value("ownGame", nlohmann::json::array());
    for (size_t index = 0; index < kOwnGameCategories.size(); index++) {
        mOwnGame[index] = std::find(ownGame.begin(), ownGame.end(), kOwnGameCategories[index].key) != ownGame.end();
    }
    mKeyRingCount = std::clamp(stored.value("keyRingCount", mKeyRingCount), 0, kKeyRingDungeons);
    const std::string rewards = stored.value("rewards", std::string("native"));
    const auto reward = std::find(kRewardPlacementKeys.begin(), kRewardPlacementKeys.end(), rewards);
    mRewards = reward == kRewardPlacementKeys.end() ? 0 : static_cast<int>(reward - kRewardPlacementKeys.begin());
}

void FleetSaveMenu::KeepLogicChoices() {
    KeepFile("logic.json", { { "songs", kSongPlacementKeys[mSongs] },
                             { "ownGame", OwnGameKeys(mOwnGame) },
                             { "keyRingCount", mKeyRingCount },
                             { "rewards", kRewardPlacementKeys[mRewards] } });
}

nlohmann::json FleetSaveMenu::GeneralChoices() const {
    nlohmann::json pools = nlohmann::json::array();
    for (size_t index = 0; index < mPools.size(); index++) {
        if (mPools[index]) {
            pools.push_back(kCrossPools[index].key);
        }
    }
    nlohmann::json passages = nlohmann::json::array();
    for (size_t index = 0; index < mPassages.size(); index++) {
        if (mPassages[index]) {
            passages.push_back(ComboRando::CrossDungeon::kPassages[index].key);
        }
    }
    return { { "start", mStart },
             { "hunt", mHunt },
             { "eitherGame", mEitherGame },
             { "required", mRequired },
             { "total", mTotal },
             { "plentifulModItems", mPlentifulModItems },
             { "crossPools", pools },
             { "passages", passages },
             { "equipmentDefaultsApplied", mEquipmentDefaultsApplied },
             { "bossGoalPieces", mBossGoalPieces } };
}

void FleetSaveMenu::LoadGeneralChoices() {
    const auto stored = nlohmann::json::parse(std::ifstream(mRoot / "general.json"), nullptr, false);
    if (!stored.is_object()) {
        return;
    }
    mStart = std::clamp(stored.value("start", mStart), 0, 2);
    mHunt = stored.value("hunt", mHunt);
    mEitherGame = stored.value("eitherGame", mEitherGame);
    mRequired = std::clamp(stored.value("required", mRequired), 1, ComboRando::kMaxComboTriforcePieces);
    mTotal = std::clamp(stored.value("total", mTotal), mRequired, ComboRando::kMaxComboTriforcePieces);
    mPlentifulModItems = stored.value("plentifulModItems", mPlentifulModItems);
    mEquipmentDefaultsApplied = stored.value("equipmentDefaultsApplied", mEquipmentDefaultsApplied);
    mBossGoalPieces =
        std::clamp(stored.value("bossGoalPieces", mBossGoalPieces), 0, ComboRando::kMaxComboTriforcePieces);
    const auto pools = stored.value("crossPools", nlohmann::json::array());
    for (size_t index = 0; index < mPools.size(); index++) {
        mPools[index] = std::find(pools.begin(), pools.end(), kCrossPools[index].key) != pools.end();
    }
    const auto passages = stored.value("passages", nlohmann::json::array());
    for (size_t index = 0; index < mPassages.size(); index++) {
        mPassages[index] = std::find(passages.begin(), passages.end(),
                                     ComboRando::CrossDungeon::kPassages[index].key) != passages.end();
    }
    mKeptGeneral = GeneralChoices();
}

void FleetSaveMenu::KeepGeneralChoices() {
    nlohmann::json current = GeneralChoices();
    if (current == mKeptGeneral) {
        return;
    }
    mKeptGeneral = std::move(current);
    KeepFile("general.json", mKeptGeneral);
}

void FleetSaveMenu::DrawItemPoolPage() {
    PrepareWorkerPage(mPoolFeed);
    const auto theme = ComboRando::ComboMenu_ThemeColor();
    DrawShareBar(theme);
    KeepGeneralChoices();
    DrawPoolSummary(theme);
    DrawStartingOptions();
    DrawOotUnshuffledNotes();
    DrawChosenStartingItems(theme);
    DrawUnshuffledStartingItems();
}

bool FleetSaveMenu::FindIcon(const std::string& game, const std::string& name, FleetItemIcon& icon) const {
    if (!mIconSource) {
        return false;
    }
    const std::string shown = Lowercase(DisplayName(game, name));
    if (const SongTint* song = FindMmSongTint(shown); song != nullptr && mIconSource("oot", "Zelda's Lullaby", icon)) {
        std::copy(song->tint.begin(), song->tint.end(), icon.tint);
        return true;
    }
    if (IsCustomItemKey(name) && IsSongName(shown) && mIconSource("oot", "Zelda's Lullaby", icon)) {
        std::fill(std::begin(icon.tint), std::end(icon.tint), 1.0f);
        return true;
    }
    if (ContainsAny(shown, { "silver:", "well silver", "silver rupee" }) && mIconSource("mm", "Silver Rupee", icon)) {
        return true;
    }
    if (mIconSource(game, name, icon)) {
        return true;
    }
    const std::string other = game == "oot" ? "mm" : "oot";
    if (const auto twin = mTwins.find({ game, name }); twin != mTwins.end() && mIconSource(other, twin->second, icon)) {
        return true;
    }
    return mIconSource(other, DisplayName(game, name), icon);
}

void FleetSaveMenu::DrawItemIcon(const std::string& game, const std::string& name, float size, bool faded) {
    FleetItemIcon icon = {};
    if (FindIcon(game, name, icon)) {
        const float alpha = icon.tint[3] * (faded ? 0.3f : 1.0f);
        ImGui::Dummy(ImVec2(size, size));
        const float height = Lowercase(DisplayName(game, name)).ends_with("owl statue") ? size / 2.0f : size;
        const ImVec2 corner = ImGui::GetItemRectMin();
        const ImVec2 top = ImVec2(corner.x, corner.y + (size - height) / 2.0f);
        ImGui::GetWindowDrawList()->AddImage(
            reinterpret_cast<ImTextureID>(reinterpret_cast<uintptr_t>(icon.texture)), top,
            ImVec2(top.x + size, top.y + height), ImVec2(icon.uv0[0], icon.uv0[1]), ImVec2(icon.uv1[0], icon.uv1[1]),
            ImGui::GetColorU32(ImVec4(icon.tint[0], icon.tint[1], icon.tint[2], alpha)));
        return;
    }
    ImGui::Dummy(ImVec2(size, size));
    const ImVec2 corner = ImGui::GetItemRectMin();
    const ImU32 color = ImGui::GetColorU32(faded ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(corner, ImVec2(corner.x + size, corner.y + size), color);
    drawList->AddText(ImVec2(corner.x + 2.0f, corner.y + (size - ImGui::GetTextLineHeight()) / 2.0f), color,
                      name.substr(0, 3).c_str());
}

void FleetSaveMenu::DrawItemCell(const ItemCell& cell) {
    constexpr float kIconSize = 32.0f;
    constexpr float kCaptionGap = 4.0f;
    const float captionWidth = ImGui::CalcTextSize("x000").x;
    ImGui::BeginGroup();
    DrawItemIcon(cell.game, cell.name, kIconSize, cell.faded);
    ImGui::SameLine(0.0f, kCaptionGap);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (kIconSize - ImGui::GetTextLineHeight()) / 2.0f);
    const ImVec4 color = cell.edited ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f)
                                     : ImGui::GetStyleColorVec4(cell.faded ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    ImGui::TextColored(color, "%s", cell.caption.c_str());
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::Dummy(ImVec2(std::max(0.0f, captionWidth - ImGui::CalcTextSize(cell.caption.c_str()).x), 0.0f));
    ImGui::EndGroup();
}

void FleetSaveMenu::DrawItemTooltip(const std::string& game, const std::string& name) {
    const int edit = PoolEdit(game, name);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    const bool isShared = mSharedInPool.contains({ game, name });
    ImGui::Text("%s (%s)", DisplayName(game, name).c_str(), isShared ? "Shared" : GameLabel(game));
    ImGui::Text("In the pool: %d", PoolCount(game, name));
    if (edit != 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(edited %+d)", edit);
    }
    ImGui::Text("Starting with: %d of %d", StartingCount(game, name), StartingMax(game, name));
    if (const auto counterpart = StartingCounterpart(game, name); counterpart && !isShared) {
        ImGui::Text("Also starts %s (%s)", DisplayName(counterpart->first, counterpart->second).c_str(),
                    GameLabel(counterpart->first));
    }
    if (const auto variants = mVariants.find({ game, name }); variants != mVariants.end()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Variants:");
        for (const std::string& variant : variants->second) {
            ImGui::BulletText("%s", variant.c_str());
        }
    }
    if (const auto tiers = mUpgradeTiers.find({ game, name }); tiers != mUpgradeTiers.end() && !tiers->second.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Upgrades, first to last:");
        for (size_t index = 0; index < tiers->second.size(); index++) {
            ImGui::Text("%zu. %s", index + 1, tiers->second[index].c_str());
        }
    }
    if (const std::string note = game == "oot" ? OotUnshuffledNote(DisplayName(game, name)) : std::string{};
        !note.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", note.c_str());
    }
    const auto description = mDescriptions.find({ game, name });
    if (description != mDescriptions.end() && !description->second.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(description->second.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void FleetSaveMenu::LoadStartingItems() {
    mStartingStale = false;
    for (const char* game : { "oot", "mm" }) {
        const std::string reply = mGameRequest ? mGameRequest(game, "StartingItems", "{}") : std::string{};
        mStarting[game] = nlohmann::json::parse(reply, nullptr, false);
        if (!mStarting[game].is_array()) {
            mStarting[game] = nlohmann::json::array();
        }
        std::sort(mStarting[game].begin(), mStarting[game].end(),
                  [](const nlohmann::json& left, const nlohmann::json& right) {
                      return Lowercase(left.value("name", std::string{})) <
                             Lowercase(right.value("name", std::string{}));
                  });
    }
}

void FleetSaveMenu::LoadFleetStarting() {
    const auto file = mRoot / "starting.json";
    if (!std::filesystem::exists(file)) {
        return;
    }
    const auto stored = nlohmann::json::parse(std::ifstream(file), nullptr, false);
    if (!stored.is_object()) {
        return;
    }
    for (const char* game : { "oot", "mm" }) {
        if (stored.value(game, nlohmann::json()).is_array()) {
            mFleetStarting[game] = stored.at(game);
        }
    }
}

void FleetSaveMenu::SaveFleetStarting() const {
    Write(mRoot / "starting.json", mFleetStarting);
}

const nlohmann::json* FleetSaveMenu::NativeStarting(const std::string& game, const std::string& name) const {
    const auto items = mStarting.find(game);
    if (items == mStarting.end()) {
        return nullptr;
    }
    for (const auto& item : items->second) {
        if (item.value("name", std::string{}) == name) {
            return &item;
        }
    }
    return nullptr;
}

int FleetSaveMenu::StartingCount(const std::string& game, const std::string& name) const {
    if (const nlohmann::json* native = NativeStarting(game, name)) {
        return native->value("count", 0);
    }
    const auto& chosen = mFleetStarting.at(game);
    return static_cast<int>(std::count(chosen.begin(), chosen.end(), nlohmann::json(name)));
}

int FleetSaveMenu::StartingCountAtPool(const std::string& game, const std::string& name) const {
    const auto native = mStartingAtPool.find(game);
    if (native != mStartingAtPool.end()) {
        for (const auto& item : native->second) {
            if (item.value("name", std::string{}) == name) {
                return item.value("count", 0);
            }
        }
    }
    const auto chosen = mFleetStartingAtPool.value(game, nlohmann::json::array());
    return static_cast<int>(std::count(chosen.begin(), chosen.end(), nlohmann::json(name)));
}

int FleetSaveMenu::ShuffledCount(const std::string& game, const std::string& name) const {
    const auto found = mPoolCopies.find({ game, name });
    const int copies = found != mPoolCopies.end() ? found->second : 0;
    const int startedSincePool = StartingCount(game, name) - StartingCountAtPool(game, name);
    return std::max(0, copies - startedSincePool);
}

int FleetSaveMenu::PoolCount(const std::string& game, const std::string& name) const {
    return std::max(0, ShuffledCount(game, name) + PoolEdit(game, name));
}

int FleetSaveMenu::StartingMax(const std::string& game, const std::string& name) const {
    if (const nlohmann::json* native = NativeStarting(game, name)) {
        return native->value("max", 1);
    }
    return std::max(1, PoolCount(game, name) + StartingCount(game, name));
}

void FleetSaveMenu::SetStartingCount(const std::string& game, const std::string& name, int count) {
    const int clamped = std::clamp(count, 0, StartingMax(game, name));
    if (NativeStarting(game, name) != nullptr) {
        if (mGameRequest) {
            mGameRequest(game, "SetStartingItem", nlohmann::json{ { "name", name }, { "count", clamped } }.dump());
        }
        return;
    }
    auto& chosen = mFleetStarting[game];
    chosen.erase(std::remove(chosen.begin(), chosen.end(), nlohmann::json(name)), chosen.end());
    for (int copy = 0; copy < clamped; copy++) {
        chosen.push_back(name);
    }
    SaveFleetStarting();
}

void FleetSaveMenu::ChangeStarting(const std::string& game, const std::string& name, int delta) {
    const int count = std::max(StartingCount(game, name) + delta, 0);
    SetStartingCount(game, name, count);
    if (const auto counterpart = StartingCounterpart(game, name)) {
        SetStartingCount(counterpart->first, counterpart->second, count);
    }
    mStartingStale = true;
}

void FleetSaveMenu::ClearStartingItems() {
    for (const char* game : { "oot", "mm" }) {
        for (const auto& item : mStarting[game]) {
            if (item.value("count", 0) > 0) {
                SetStartingCount(game, item.value("name", std::string{}), 0);
            }
        }
        mFleetStarting[game] = nlohmann::json::array();
    }
    SaveFleetStarting();
    mStartingStale = true;
}

void FleetSaveMenu::DrawStartingCell(const ItemCell& cell) {
    const int count = StartingCount(cell.game, cell.name);
    DrawItemCell(cell);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && count < StartingMax(cell.game, cell.name)) {
        ChangeStarting(cell.game, cell.name, 1);
    } else if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && count > 0) {
        ChangeStarting(cell.game, cell.name, -1);
    }
    if (ImGui::IsItemHovered()) {
        DrawItemTooltip(cell.game, cell.name);
    }
}

void FleetSaveMenu::DrawPoolCell(const ItemCell& cell) {
    DrawItemCell(cell);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
        StartingCount(cell.game, cell.name) < StartingMax(cell.game, cell.name)) {
        ChangeStarting(cell.game, cell.name, 1);
    }
    if (ImGui::IsItemHovered()) {
        DrawItemTooltip(cell.game, cell.name);
    }
    if (ImGui::BeginPopupContextItem("poolCell")) {
        DrawPoolCellMenu(cell.game, cell.name);
        ImGui::EndPopup();
    }
}

void FleetSaveMenu::DrawPoolCellMenu(const std::string& game, const std::string& name) {
    const int starting = StartingCount(game, name);
    const int copies = PoolCount(game, name);
    const int edit = PoolEdit(game, name);
    ImGui::TextDisabled("%s (%s)", DisplayName(game, name).c_str(), GameLabel(game));
    if (ImGui::MenuItem("Start with one more", nullptr, false, starting < StartingMax(game, name))) {
        ChangeStarting(game, name, 1);
    }
    if (ImGui::MenuItem("Start with one less", nullptr, false, starting > 0)) {
        ChangeStarting(game, name, -1);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Add a copy to the pool")) {
        ChangePoolEdit(game, name, 1);
    }
    if (ImGui::MenuItem("Remove a copy from the pool", nullptr, false, copies > 0)) {
        ChangePoolEdit(game, name, -1);
    }
    if (ImGui::MenuItem("Remove every copy", nullptr, false, copies > 0)) {
        ChangePoolEdit(game, name, -copies);
    }
    if (ImGui::MenuItem("Restore the pool's copies", nullptr, false, edit != 0)) {
        ChangePoolEdit(game, name, -edit);
    }
}

std::string FleetSaveMenu::DisplayName(const std::string& game, const std::string& name) const {
    const auto found = mDisplayNames.find({ game, name });
    return found != mDisplayNames.end() ? found->second : name;
}

bool FleetSaveMenu::MatchesItemFilter(const std::string& filter, const std::string& game,
                                      const std::string& name) const {
    return filter.empty() || Lowercase(DisplayName(game, name)).find(filter) != std::string::npos;
}

void FleetSaveMenu::SortCells(std::vector<ItemCell>& cells) const {
    std::sort(cells.begin(), cells.end(), [this](const ItemCell& left, const ItemCell& right) {
        return Lowercase(DisplayName(left.game, left.name)) < Lowercase(DisplayName(right.game, right.name));
    });
}

void FleetSaveMenu::DrawCellsByGame(const char* id, std::vector<ItemCell> cells) {
    std::array<std::vector<ItemCell>, 2> byGame;
    for (auto& cell : cells) {
        byGame[cell.game == "oot" ? 0 : 1].push_back(std::move(cell));
    }
    const bool bothGames = !byGame[0].empty() && !byGame[1].empty();
    const float available = ImGui::GetContentRegionAvail().x;
    const float width = bothGames ? (available - ImGui::GetStyle().ItemSpacing.x) / 2.0f : available;
    constexpr std::array<const char*, 2> kTitles = { "Ocarina of Time", "Majora's Mask" };
    ImGui::PushID(id);
    for (size_t game = 0; game < byGame.size(); game++) {
        if (byGame[game].empty()) {
            continue;
        }
        if (game == 1 && bothGames) {
            ImGui::SameLine();
        }
        DrawCellBox(kTitles[game], byGame[game], width);
    }
    ImGui::PopID();
}

void FleetSaveMenu::DrawCellBox(const char* title, const std::vector<ItemCell>& cells, float width) {
    ImGui::BeginChild(title, ImVec2(width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::TextDisabled("%s", title);
    for (const auto& cell : cells) {
        ImGui::PushID((cell.game + cell.name).c_str());
        if (cell.inPool) {
            DrawPoolCell(cell);
        } else {
            DrawStartingCell(cell);
        }
        ImGui::PopID();
        WrapCell();
    }
    ImGui::NewLine();
    ImGui::EndChild();
}

void FleetSaveMenu::DrawStartingOptions() {
    ImGui::SeparatorText("Starting Options");
    if (!ImGui::BeginTable("startingOptions", 3, ImGuiTableFlags_SizingStretchSame)) {
        return;
    }
    ImGui::TableNextColumn();
    DrawOotHeartSlider("%d Hearts", "gRandoSettings.StartingHearts", 1, 3);
    ImGui::TableNextColumn();
    DrawOotHeartSlider("%d Golden Hearts", "gRandoSettings.Mods.oot.starting_golden_hearts", 0, 0);
    ImGui::TableNextColumn();
    constexpr std::array<const char*, 4> kPocketChoices = { "Dungeon Reward", "Advancement", "Anything", "Nothing" };
    DrawGameCombo("Link's Pocket", "oot", "gRandoSettings.LinksPocket", kPocketChoices);
    ComboRando::ComboMenu_PushCheckbox(ComboRando::ComboMenu_ThemeColor());
    ImGui::TableNextColumn();
    DrawGameCheckbox("Full Wallets", "oot", "gRandoSettings.FullWallets");
    ImGui::TableNextColumn();
    DrawGameCheckbox("Full Deku Sticks and Nuts (MM)", "mm", "gRando.Options.RO_STARTING_CONSUMABLES");
    ComboRando::ComboMenu_PopCheckbox();
    ImGui::EndTable();
}

bool FleetSaveMenu::IsWithheldFromMmStart(const std::string& game, const std::string& name) const {
    if (mStart != kOotStartIndex || game != "mm") {
        return false;
    }
    for (const ComboRando::SharedFamily family : ComboRando::kMmEntryFamilies) {
        const auto& definition = ComboRando::SharedFamilyByIndex(family);
        if (name == definition.mmName) {
            return StartingCount("oot", definition.ootName) == 0;
        }
    }
    return false;
}

void FleetSaveMenu::DrawChosenStartingItems(const ImVec4& theme) {
    ImGui::SeparatorText("Starting Items");
    ComboRando::ComboMenu_PushButton(theme);
    if (ImGui::Button("Start With Nothing")) {
        ClearStartingItems();
    }
    ComboRando::ComboMenu_PopButton();
    ImGui::SameLine();
    ImGui::TextDisabled("Left click a starting item for one more copy, right click for one less. An item both games "
                        "have starts in both.");
    std::vector<ItemCell> cells;
    for (const char* game : { "oot", "mm" }) {
        std::vector<std::string> names;
        for (const auto& item : mStarting[game]) {
            const std::string name = item.value("name", std::string{});
            if (item.value("count", 0) > 0 && !IsWithheldFromMmStart(game, name)) {
                names.push_back(name);
            }
        }
        for (const auto& name : mFleetStarting.at(game)) {
            if (std::find(names.begin(), names.end(), name.get<std::string>()) == names.end()) {
                names.push_back(name.get<std::string>());
            }
        }
        for (const auto& name : names) {
            cells.push_back({ game, name, "x" + std::to_string(StartingCount(game, name)), false, false, false });
        }
    }
    if (cells.empty()) {
        ImGui::TextDisabled("Nothing: click an item of the pool above to start with it.");
        return;
    }
    SortCells(cells);
    DrawCellsByGame("chosen", std::move(cells));
}

void FleetSaveMenu::DrawPoolEditsBar(const ImVec4& theme) {
    ImGui::BeginDisabled(mPoolEdits.empty());
    ComboRando::ComboMenu_PushButton(theme);
    if (ImGui::Button("Reset edits")) {
        mPoolEdits.clear();
        KeepFile("pool_edits.json", PoolEditsJson());
    }
    ComboRando::ComboMenu_PopButton();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (mPoolEdits.empty()) {
        ImGui::TextDisabled("No pool edits.");
        return;
    }
    ImGui::TextDisabled("%zu items edited; junk makes up the difference.", mPoolEdits.size());
}

void FleetSaveMenu::DrawPoolSummary(const ImVec4& theme) {
    ImGui::SeparatorText("Item Pool");
    ImGui::SetNextItemWidth(260);
    ComboRando::ComboMenu_PushInput(theme);
    ImGui::InputTextWithHint("##itemFilter", "Search items...", mItemFilter.data(), mItemFilter.size());
    ComboRando::ComboMenu_PopInput();
    ImGui::SameLine();
    DrawPoolEditsBar(theme);
    DrawWorkerStatus(mPoolFeed, "Every item the current options shuffle in both games, mods included.");
    ImGui::TextDisabled("Left click an item to start with one more copy. Right click it to start with one less, or "
                        "to add or remove copies in the pool.");
    const std::string filter = Lowercase(mItemFilter.data());
    std::array<std::vector<ItemCell>, kPoolCategoryLabels.size()> sections;
    std::array<int, kPoolCategoryLabels.size()> totals = {};
    std::vector<const nlohmann::json*> junk;
    int junkBalance = 0;
    for (const auto& item : mPool) {
        if (IsJunkItem(item)) {
            junk.push_back(&item);
            continue;
        }
        const std::string game = item.value("game", std::string{});
        const std::string name = item.value("name", std::string{});
        const int copies = PoolCount(game, name);
        const size_t section = static_cast<size_t>(ClassifyPoolItem(item));
        junkBalance += ShuffledCount(game, name) - copies;
        totals[section] += copies;
        if (MatchesItemFilter(filter, game, name)) {
            const bool edited = PoolEdit(game, name) != 0;
            sections[section].push_back({ game, name, "x" + std::to_string(copies), copies == 0, edited, true });
        }
    }
    for (size_t index = 0; index < sections.size(); index++) {
        const bool isRewards = index == static_cast<size_t>(PoolCategory::DungeonRewards);
        if (sections[index].empty() && !isRewards) {
            continue;
        }
        const char* label = kPoolCategoryLabels[index];
        ImGui::SeparatorText((std::string(label) + " (" + std::to_string(totals[index]) + ")").c_str());
        if (isRewards) {
            DrawDungeonRewardPlacement();
        }
        DrawCellsByGame(label, std::move(sections[index]));
    }
    DrawJunkCell(junk, junkBalance, filter);
}

void FleetSaveMenu::DrawDungeonRewardPlacement() {
    const auto row =
        std::find_if(kDungeonItemRows.begin(), kDungeonItemRows.end(), [](const DungeonItemRow& candidate) {
            return std::string_view(candidate.ownGameKey) == "dungeonReward";
        });
    if (row != kDungeonItemRows.end()) {
        DrawDungeonItemRow(*row);
    }
}

void FleetSaveMenu::DrawJunkCell(const std::vector<const nlohmann::json*>& junk, int balance,
                                 const std::string& filter) {
    if (junk.empty()) {
        return;
    }
    const bool matches =
        std::string("junk").find(filter) != std::string::npos ||
        std::any_of(junk.begin(), junk.end(), [&](const nlohmann::json* item) {
            return MatchesItemFilter(filter, item->value("game", std::string{}), item->value("name", std::string{}));
        });
    if (!matches) {
        return;
    }
    int copies = balance;
    for (const nlohmann::json* item : junk) {
        copies += ShuffledCount(item->value("game", std::string{}), item->value("name", std::string{}));
    }
    copies = std::max(0, copies);
    const auto largest =
        std::max_element(junk.begin(), junk.end(), [](const nlohmann::json* left, const nlohmann::json* right) {
            return PoolCopies(*left) < PoolCopies(*right);
        });
    ImGui::SeparatorText(("Junk (" + std::to_string(copies) + ")").c_str());
    ImGui::PushID("junk");
    DrawItemCell({ (*largest)->value("game", std::string{}), (*largest)->value("name", std::string{}),
                   "x" + std::to_string(copies), false, balance != 0, false });
    ImGui::PopID();
    if (!ImGui::IsItemHovered()) {
        return;
    }
    ImGui::BeginTooltip();
    ImGui::Text("Junk: %d copies of rupees, recovery hearts, ammo and the like.", copies);
    for (const nlohmann::json* item : junk) {
        const std::string game = item->value("game", std::string{});
        const std::string name = item->value("name", std::string{});
        ImGui::TextDisabled("%s (%s) x%d", DisplayName(game, name).c_str(), GameLabel(game), ShuffledCount(game, name));
    }
    ImGui::EndTooltip();
}

int FleetSaveMenu::PoolEdit(const std::string& game, const std::string& name) const {
    const auto found = mPoolEdits.find({ game, name });
    return found != mPoolEdits.end() ? found->second : 0;
}

void FleetSaveMenu::ChangePoolEdit(const std::string& game, const std::string& name, int delta) {
    const int edit = PoolEdit(game, name) + delta;
    if (edit == 0) {
        mPoolEdits.erase({ game, name });
    } else {
        mPoolEdits[{ game, name }] = edit;
    }
    KeepFile("pool_edits.json", PoolEditsJson());
}

nlohmann::json FleetSaveMenu::PoolEditsJson() const {
    nlohmann::json edits = nlohmann::json::array();
    for (const auto& [key, delta] : mPoolEdits) {
        edits.push_back({ { "game", key.first }, { "item", key.second }, { "delta", delta } });
    }
    return edits;
}

void FleetSaveMenu::LoadPoolEdits() {
    const auto stored = nlohmann::json::parse(std::ifstream(mRoot / "pool_edits.json"), nullptr, false);
    if (!stored.is_array()) {
        return;
    }
    try {
        for (const auto& edit : stored) {
            const int delta = edit.value("delta", 0);
            if (delta != 0) {
                mPoolEdits[{ edit.value("game", std::string{}), edit.value("item", std::string{}) }] = delta;
            }
        }
    } catch (const nlohmann::json::exception&) { mPoolEdits.clear(); }
}

void FleetSaveMenu::DrawUnshuffledStartingItems() {
    if (mPool.empty()) {
        return;
    }
    const std::string filter = Lowercase(mItemFilter.data());
    std::vector<ItemCell> cells;
    for (const char* game : { "oot", "mm" }) {
        for (const auto& item : mStarting[game]) {
            const std::string name = item.value("name", std::string{});
            if (IsCoveredByPool(game, name) || item.value("count", 0) != 0 || !MatchesItemFilter(filter, game, name)) {
                continue;
            }
            const bool startsAnyway = std::string(game) == "oot" &&
                                      Lowercase(DisplayName(game, name)) == "gerudo membership card" &&
                                      StartsWithUnshuffledGerudoCard();
            cells.push_back({ game, name, startsAnyway ? "Start" : "", !startsAnyway, false, false });
        }
    }
    if (cells.empty()) {
        return;
    }
    const std::string header = "Not in the Pool (" + std::to_string(cells.size()) + ")";
    if (!ImGui::CollapsingHeader(header.c_str())) {
        return;
    }
    ImGui::TextDisabled("Items these options leave out of the pool. Click one to start with it.");
    DrawCellsByGame("unshuffled", std::move(cells));
}

void FleetSaveMenu::WrapCell() {
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const float next = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + ImGui::GetItemRectSize().x;
    if (next < right) {
        ImGui::SameLine();
    }
}

static const char* ShareRowIcon(int family, const char* ootName) {
    switch (family) {
        case ComboRando::SF_LIVE_RUPEES:
            return "Huge Rupee";
        case ComboRando::SF_LIVE_HEALTH:
            return "Heart Container";
        case ComboRando::SF_LIVE_MAGIC:
            return "Progressive Magic Meter";
        case ComboRando::SF_LIVE_EQUIPS:
            return "Progressive Bow";
        default:
            return ootName;
    }
}

void FleetSaveMenu::RebuildShareRows(const nlohmann::json& modPairs) {
    mShareRows.clear();
    for (int family = 0; family < ComboRando::SF_COUNT; family++) {
        if (ComboRando::IsFirstOfSharedOption(family)) {
            const auto& definition = ComboRando::SharedFamilyByIndex(family);
            mShareRows.push_back({ ShareLabel(definition),
                                   definition.tooltip,
                                   family,
                                   {},
                                   "oot",
                                   ShareRowIcon(family, definition.ootName) });
        }
    }
    for (const auto& pair : modPairs) {
        const std::string key = pair.value("key", std::string{});
        if (!ComboRando::IsSharePairRow(key, modPairs)) {
            continue;
        }
        const std::string ootName = pair.value("oot", std::string{});
        const bool hasOotName = !ootName.empty();
        mShareRows.push_back({ ModPairLabel(pair), "One copy counts for both games while shared.\n" + key, kModPairRow,
                               key, hasOotName ? "oot" : "mm",
                               hasOotName ? ootName : pair.value("mm", std::string{}) });
    }
    NameFoldedPairs(modPairs);
    TellTwinRowsApart();
    std::sort(mShareRows.begin(), mShareRows.end(), [](const ShareRow& left, const ShareRow& right) {
        return Lowercase(left.label) < Lowercase(right.label);
    });
}

FleetSaveMenu::ShareRow* FleetSaveMenu::FindHostRow(const ComboRando::FoldedPair& folded) {
    const auto host = std::find_if(mShareRows.begin(), mShareRows.end(), [&](const ShareRow& row) {
        return row.family == folded.hostFamily && row.pairKey == folded.hostPair;
    });
    return host == mShareRows.end() ? nullptr : &*host;
}

void FleetSaveMenu::NameFoldedPairs(const nlohmann::json& modPairs) {
    for (const auto& pair : modPairs) {
        const std::string key = pair.value("key", std::string{});
        const ComboRando::FoldedPair* folded = ComboRando::FindFoldedPair(key);
        ShareRow* host = folded != nullptr ? FindHostRow(*folded) : nullptr;
        if (host != nullptr) {
            host->tooltip += "\nAlso decides " + key + ".";
        }
    }
}

void FleetSaveMenu::TellTwinRowsApart() {
    std::map<std::string, int> rowsPerLabel;
    for (const auto& row : mShareRows) {
        rowsPerLabel[row.label]++;
    }
    for (auto& row : mShareRows) {
        if (row.family == kModPairRow && rowsPerLabel[row.label] > 1) {
            row.label += " (" + row.pairKey + ")";
        }
    }
}

bool FleetSaveMenu::MatchesShareFilter(const ShareRow& row, const std::string& filter) const {
    return filter.empty() || Lowercase(row.label).find(filter) != std::string::npos ||
           Lowercase(row.pairKey).find(filter) != std::string::npos;
}

bool FleetSaveMenu::IsShared(const ShareRow& row) const {
    return row.family == kModPairRow ? mShareChoices.IsPairShared(row.pairKey)
                                     : mShareChoices.IsFamilyShared(row.family);
}

void FleetSaveMenu::SetShared(const ShareRow& row, bool shared) {
    if (row.family == kModPairRow) {
        mShareChoices.SetPairShared(row.pairKey, shared);
    } else {
        mShareChoices.SetFamilyShared(row.family, shared);
    }
}

void FleetSaveMenu::SetAllShared(bool shared) {
    mShareChoices.SetAllShared(shared);
    KeepShareChoices();
}

std::vector<std::string> FleetSaveMenu::PairRowKeys() const {
    std::vector<std::string> keys;
    for (const auto& row : mShareRows) {
        if (row.family == kModPairRow) {
            keys.push_back(row.pairKey);
        }
    }
    return keys;
}

// A generation with a bulk choice still pending would share the mod pairs the player unshared.
void FleetSaveMenu::GenerateOncePoolArrives() {
    if (!mGenerateWhenPoolArrives) {
        return;
    }
    if (!mPoolFeed.error.empty()) {
        mGenerateWhenPoolArrives = false;
        throw std::runtime_error("The item pool could not be computed, so the share choice cannot reach the mod "
                                 "items: " +
                                 mPoolFeed.error);
    }
    if (mShareChoices.IsBulkChoicePending()) {
        PrepareWorkerPage(mPoolFeed);
        return;
    }
    mGenerateWhenPoolArrives = false;
    Generate();
}

void FleetSaveMenu::LoadShareChoices() {
    mShareChoices.ReadJson(nlohmann::json::parse(std::ifstream(mRoot / "shared.json"), nullptr, false));
}

void FleetSaveMenu::KeepShareChoices() {
    KeepFile("shared.json", mShareChoices.ToJson());
}

void FleetSaveMenu::DrawShareRow(const ShareRow& row) {
    const bool shared = IsShared(row);
    bool chosen = shared;
    ImGui::PushID(row.label.c_str());
    if (ImGui::Checkbox("##shared", &chosen)) {
        SetShared(row, chosen);
        KeepShareChoices();
    }
    ImGui::SameLine();
    DrawItemIcon(row.iconGame, row.iconItem, ImGui::GetFrameHeight(), !shared);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(row.label.c_str());
    ImGui::SetItemTooltip("%s", row.tooltip.c_str());
    ImGui::PopID();
}

void FleetSaveMenu::DrawShareList(const ImVec4& theme) {
    constexpr float kListHeight = 220.0f;
    constexpr int kColumns = 3;
    const std::string filter = Lowercase(mShareFilter.data());
    std::vector<const ShareRow*> shown;
    for (const auto& row : mShareRows) {
        if (MatchesShareFilter(row, filter)) {
            shown.push_back(&row);
        }
    }
    const ImGuiTableFlags flags =
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter;
    if (!ImGui::BeginTable("shareList", kColumns, flags, ImVec2(0.0f, kListHeight))) {
        return;
    }
    ComboRando::ComboMenu_PushCheckbox(theme);
    ImGuiListClipper clipper;
    clipper.Begin((static_cast<int>(shown.size()) + kColumns - 1) / kColumns);
    while (clipper.Step()) {
        for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; line++) {
            ImGui::TableNextRow();
            for (int column = 0; column < kColumns; column++) {
                const size_t index = static_cast<size_t>(line) * kColumns + column;
                if (index >= shown.size()) {
                    break;
                }
                ImGui::TableSetColumnIndex(column);
                DrawShareRow(*shown[index]);
            }
        }
    }
    ComboRando::ComboMenu_PopCheckbox();
    ImGui::EndTable();
}

void FleetSaveMenu::DrawShareBar(const ImVec4& theme) {
    ImGui::SeparatorText("Shared Items");
    ComboRando::ComboMenu_PushButton(theme);
    if (ImGui::Button("Share All")) {
        SetAllShared(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Unshare All")) {
        SetAllShared(false);
    }
    ComboRando::ComboMenu_PopButton();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260);
    ComboRando::ComboMenu_PushInput(theme);
    ImGui::InputTextWithHint("##shareFilter", "Search items...", mShareFilter.data(), mShareFilter.size());
    ComboRando::ComboMenu_PopInput();
    ImGui::SameLine();
    const auto sharedCount =
        std::count_if(mShareRows.begin(), mShareRows.end(), [this](const ShareRow& row) { return IsShared(row); });
    ImGui::TextDisabled("%d of %d shared", static_cast<int>(sharedCount), static_cast<int>(mShareRows.size()));
    ImGui::SameLine();
    ComboRando::ComboMenu_PushCheckbox(theme);
    ImGui::Checkbox("Plentiful mod items", &mPlentifulModItems);
    ComboRando::ComboMenu_PopCheckbox();
    ImGui::SetItemTooltip("Mod items get an extra copy in the pool.");
    DrawShareList(theme);
    ImGui::TextDisabled(mShareChoices.ArePairsKnown() ? "A shared item counts for both games. Applied at generation."
                                       : "Mod items join this list once the item pool is computed.");
}

void FleetSaveMenu::DrawPlandomizerPage() {
    PrepareWorkerPage(mChecksFeed);
    const auto theme = ComboRando::ComboMenu_ThemeColor();
    ImGui::TextWrapped("Choose what a check holds. Random leaves it to the generator, Vanilla keeps the check's own "
                       "item and Junk keeps anything needed out of it. Logic still validates the seed.");
    DrawWorkerStatus(mChecksFeed, "Every check of both games under the current options.");
    if (mChecks.empty()) {
        return;
    }
    DrawPlandoFilters(theme);
    FilterChecks();
    DrawPlandoBulk(theme);
    ComboRando::ComboMenu_PushCombobox(theme);
    DrawPlandoTable();
    ComboRando::ComboMenu_PopCombobox();
}

nlohmann::json FleetSaveMenu::PlandoJson() const {
    nlohmann::json plando = nlohmann::json::array();
    for (const auto& [key, choice] : mPlando) {
        nlohmann::json entry = { { "game", key.first }, { "check", key.second }, { "state", choice.state } };
        if (choice.state == "item") {
            entry["itemGame"] = choice.itemGame;
            entry["item"] = choice.item;
        }
        plando.push_back(std::move(entry));
    }
    return plando;
}

void FleetSaveMenu::LoadPlando() {
    const auto stored = nlohmann::json::parse(std::ifstream(mRoot / "plando.json"), nullptr, false);
    if (!stored.is_array()) {
        return;
    }
    try {
        for (const auto& entry : stored) {
            const std::string state = entry.value("state", std::string{});
            if (state != "vanilla" && state != "junk" && state != "item") {
                continue;
            }
            mPlando[{ entry.value("game", std::string{}), entry.value("check", std::string{}) }] = {
                state, entry.value("itemGame", std::string{}), entry.value("item", std::string{})
            };
        }
    } catch (const nlohmann::json::exception&) { mPlando.clear(); }
}

void FleetSaveMenu::KeepPlando() {
    KeepFile("plando.json", PlandoJson());
}

void FleetSaveMenu::SetPlando(const PlandoCheck& check, std::optional<PlandoChoice> choice) {
    if (!choice) {
        mPlando.erase({ check.game, check.name });
        return;
    }
    mPlando[{ check.game, check.name }] = std::move(*choice);
}

void FleetSaveMenu::FilterChecks() {
    const std::string filter = Lowercase(mCheckFilter.data());
    const std::string key = filter + '\n' + std::to_string(mAreaFilter) + '\n' + std::to_string(mGameFilter) + '\n' +
                            std::to_string(mChecksVersion);
    if (key == mShownChecksKey) {
        return;
    }
    mShownChecksKey = key;
    mShownChecks.clear();
    const std::string game = mGameFilter == 1 ? "oot" : mGameFilter == 2 ? "mm" : "";
    const std::string area = mAreaFilter > 0 ? mAreas[mAreaFilter - 1] : "";
    for (size_t index = 0; index < mChecks.size(); index++) {
        const PlandoCheck& check = mChecks[index];
        if (!game.empty() && check.game != game) {
            continue;
        }
        if (!area.empty() && check.area != area) {
            continue;
        }
        if (!filter.empty() && Lowercase(check.name + " " + check.area).find(filter) == std::string::npos) {
            continue;
        }
        mShownChecks.push_back(index);
    }
}

void FleetSaveMenu::DrawPlandoFilters(const ImVec4& theme) {
    ImGui::SetNextItemWidth(260);
    ComboRando::ComboMenu_PushInput(theme);
    ImGui::InputTextWithHint("##checkFilter", "Search checks...", mCheckFilter.data(), mCheckFilter.size());
    ComboRando::ComboMenu_PopInput();
    ImGui::SameLine();
    ComboRando::ComboMenu_PushCombobox(theme);
    ImGui::SetNextItemWidth(240);
    const char* areaPreview = mAreaFilter > 0 ? mAreas[mAreaFilter - 1].c_str() : "All Areas";
    if (ImGui::BeginCombo("##areaFilter", areaPreview, ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("All Areas", mAreaFilter == 0)) {
            mAreaFilter = 0;
        }
        for (size_t index = 0; index < mAreas.size(); index++) {
            if (ImGui::Selectable(mAreas[index].c_str(), mAreaFilter == static_cast<int>(index) + 1)) {
                mAreaFilter = static_cast<int>(index) + 1;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170);
    ImGui::Combo("##gameFilter", &mGameFilter, "Both Games\0Ocarina of Time\0Majora's Mask\0");
    ComboRando::ComboMenu_PopCombobox();
}

void FleetSaveMenu::DrawPlandoBulk(const ImVec4& theme) {
    ImGui::TextDisabled("%zu of %zu checks fixed. Set the %zu shown checks to:", mPlando.size(), mChecks.size(),
                        mShownChecks.size());
    ComboRando::ComboMenu_PushButton(theme);
    for (const PlandoState& state : kBulkStates) {
        ImGui::SameLine();
        if (!ImGui::SmallButton(state.label)) {
            continue;
        }
        const std::string key = state.key;
        for (const size_t index : mShownChecks) {
            const PlandoCheck& check = mChecks[index];
            if (key == "vanilla" && check.vanilla.empty()) {
                continue;
            }
            SetPlando(check, key.empty() ? std::nullopt : std::optional<PlandoChoice>(PlandoChoice{ key, {}, {} }));
        }
        KeepPlando();
    }
    ComboRando::ComboMenu_PopButton();
}

void FleetSaveMenu::DrawPlandoTable() {
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp;
    const float height = std::max(320.0f, ImGui::GetContentRegionAvail().y);
    if (!ImGui::BeginTable("plando", 4, flags, ImVec2(0.0f, height))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Area", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Placement", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(mShownChecks.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
            ImGui::PushID(static_cast<int>(mShownChecks[row]));
            DrawPlandoRow(mChecks[mShownChecks[row]]);
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

void FleetSaveMenu::DrawPlandoRow(const PlandoCheck& check) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", GameLabel(check.game));
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(check.name.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(check.area.c_str());
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##placement", DescribePlando(check).c_str(), ImGuiComboFlags_HeightLarge)) {
        DrawItemPicker(check);
        ImGui::EndCombo();
    }
}

std::string FleetSaveMenu::DescribePlando(const PlandoCheck& check) const {
    const auto chosen = mPlando.find({ check.game, check.name });
    if (chosen == mPlando.end()) {
        return "Random";
    }
    const PlandoChoice& choice = chosen->second;
    if (choice.state == "vanilla") {
        return "Vanilla: " + DisplayName(check.game, check.vanilla);
    }
    if (choice.state == "junk") {
        return "Junk";
    }
    return DisplayName(choice.itemGame, choice.item) + " (" + GameLabel(choice.itemGame) + ")";
}

void FleetSaveMenu::DrawItemPicker(const PlandoCheck& check) {
    constexpr float kIconSize = 20.0f;
    constexpr float kListHeight = 280.0f;
    const auto chosen = mPlando.find({ check.game, check.name });
    const PlandoChoice current = chosen != mPlando.end() ? chosen->second : PlandoChoice{};
    if (ImGui::Selectable("Random", current.state.empty())) {
        SetPlando(check, std::nullopt);
        KeepPlando();
    }
    ImGui::BeginDisabled(check.vanilla.empty());
    if (ImGui::Selectable(("Vanilla: " + DisplayName(check.game, check.vanilla)).c_str(), current.state == "vanilla")) {
        SetPlando(check, PlandoChoice{ "vanilla", {}, {} });
        KeepPlando();
    }
    ImGui::EndDisabled();
    if (ImGui::Selectable("Junk", current.state == "junk")) {
        SetPlando(check, PlandoChoice{ "junk", {}, {} });
        KeepPlando();
    }
    ImGui::SeparatorText("Item");
    if (ImGui::IsWindowAppearing()) {
        mPickerFilter.fill(0);
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##pickerFilter", "Search items...", mPickerFilter.data(), mPickerFilter.size());
    const std::string filter = Lowercase(mPickerFilter.data());
    std::vector<const PlandoItem*> shown;
    for (const auto& item : mPlandoItems) {
        if (filter.empty() || Lowercase(item.label).find(filter) != std::string::npos) {
            shown.push_back(&item);
        }
    }
    ImGui::BeginChild("##pickerItems", ImVec2(0.0f, kListHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(shown.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
            const PlandoItem& item = *shown[row];
            const bool selected = current.state == "item" && current.itemGame == item.game && current.item == item.name;
            ImGui::PushID(row);
            DrawItemIcon(item.game, item.name, kIconSize, false);
            ImGui::SameLine();
            if (ImGui::Selectable(item.label.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(0.0f, kIconSize))) {
                SetPlando(check, PlandoChoice{ "item", item.game, item.name });
                KeepPlando();
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
}
