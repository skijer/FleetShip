#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "ComboPlaythrough.h"
#include "CrossWorldRando.h"
#include "MagicMirrorShuffle.h"
#include "MmEntryGate.h"

// Turns a finished Fleet fill into an Archipelago slot: its free checks become locations, their items the slot's
// pool, and each location's logic a list of item sets that reach it, measured with the games' own oracles.
namespace ComboRando::Archipelago {

inline constexpr const char* kGameName = "Fleet";
inline constexpr const char* kForeignKeyPrefix = "archipelago:";
inline constexpr int kExportFormat = 1;
inline constexpr const char* kGanonCheck = "Ganon";
inline constexpr const char* kMajoraCheck = "Moon Majora Pot 01";

inline std::string QualifiedName(GameId game, const std::string& name) {
    return (game == GAME_OOT ? "OoT: " : "MM: ") + name;
}

inline const char* GameKey(GameId game) {
    return game == GAME_OOT ? "oot" : "mm";
}

// Ids stay the same across exports, so one installed apworld keeps serving later seeds of the same games.
inline int64_t NameId(const std::string& name) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char character : name) {
        hash = (hash ^ character) * 1099511628211ULL;
    }
    const int64_t id = static_cast<int64_t>(hash & ((uint64_t{ 1 } << 53) - 1));
    return id == 0 ? 1 : id;
}

inline std::string ForeignKey(int player, int64_t item) {
    return std::string(kForeignKeyPrefix) + std::to_string(player) + ":" + std::to_string(item);
}

struct ItemFacts {
    bool advancement = false;
    bool major = false;
    std::string category;
    bool song = false;
};

using FactsByName = std::map<std::pair<GameId, std::string>, ItemFacts>;

inline FactsByName ReadItemFacts(const std::string& ootDump, const std::string& mmDump) {
    FactsByName facts;
    const std::pair<GameId, const std::string*> dumps[] = { { GAME_OOT, &ootDump }, { GAME_MM, &mmDump } };
    for (const auto& [game, text] : dumps) {
        const nlohmann::json dump = nlohmann::json::parse(*text, nullptr, false);
        if (!dump.is_object()) {
            continue;
        }
        for (const auto& item : dump.value("items", nlohmann::json::array())) {
            ItemFacts& entry = facts[{ game, item.value("name", std::string{}) }];
            entry.advancement |= item.value("advancement", false);
            entry.song |= item.value("song", false);
        }
        for (const char* section : { "pool", "fixed" }) {
            for (const auto& item : dump.value(section, nlohmann::json::array())) {
                const std::string name = item.value(section == std::string("pool") ? "name" : "item", std::string{});
                ItemFacts& entry = facts[{ game, name }];
                const bool advancement = item.value("advancement", true);
                entry.advancement |= advancement;
                entry.major |= item.value("major", advancement);
                if (entry.category.empty()) {
                    entry.category = item.value("category", std::string{});
                }
            }
        }
    }
    return facts;
}

struct ExportInput {
    std::string fillSpoiler;
    std::string ootDump;
    std::string mmDump;
    OracleFns ootOracle;
    OracleFns mmOracle;
    bool portalGated = true;
    bool mmStart = false;
    CwGoal goal;
    // When set, the goal is owning this item instead of the boss or hunt goal.
    std::string goalItem;
    GameId goalItemGame = GAME_OOT;
    uint64_t sharedMask = 0;
    std::string unsharedSpec;
    std::string songs = "native";
    std::string exportId;
    std::string mode;
};

struct Reach {
    std::unordered_set<std::string> oot;
    std::unordered_set<std::string> mm;
    bool isGoalMet = false;
    std::vector<bool> collectedLocked;
    bool isPortalOpen = false;

    bool Has(GameId game, const std::string& check) const {
        return (game == GAME_OOT ? oot : mm).contains(check);
    }
};

// One reachability search over both games, collecting locked placements as their checks open, the way the
// playthrough walks a seed.
class Reacher {
  public:
    Reacher(const ExportInput& input, std::vector<CwPlacedItem> locked)
        : mInput(input), mLocked(std::move(locked)),
          mGroups(BuildSharedItemGroups(ResolveSharedPairs(input.unsharedSpec, input.mmDump, input.ootDump),
                                        SharedMaskForSpoiler(input.fillSpoiler, input.sharedMask))),
          mEntryItems(MmEntryItems(input.mmDump)), mIsMmOpenFromStart(!input.portalGated || input.mmStart),
          mIsCrossDungeon(input.ootOracle.SetDungeonAccess && input.mmOracle.SetDungeonAccess) {
    }

    // A known subset's reach starts the search: reach only grows with items, so what the subset collected is
    // collected again, and the oracle is not asked to rediscover it round by round.
    const Reach& Query(const std::vector<CwPlacedItem>& owned, const Reach* knownSubset = nullptr) {
        std::vector<std::string> ootOwned;
        std::vector<std::string> mmOwned;
        for (const CwPlacedItem& item : owned) {
            (item.itemGame == GAME_OOT ? ootOwned : mmOwned).push_back(item.item);
        }
        const std::string key = CanonicalJointOwnedKey(ootOwned, mmOwned);
        if (const auto known = mMemo.find(key); known != mMemo.end()) {
            return known->second;
        }
        mQueries++;
        return mMemo.emplace(key, Search(std::move(ootOwned), std::move(mmOwned), knownSubset)).first->second;
    }

    std::string DescribeCost() const {
        return std::to_string(mQueries) + " searches, " + std::to_string(mOracleCalls) + " oracle calls in " +
               std::to_string(mOracleMs) + " ms";
    }

  private:
    Reach Search(std::vector<std::string> ootOwned, std::vector<std::string> mmOwned, const Reach* knownSubset) {
        std::vector<bool> collected(mLocked.size(), false);
        bool isPortalOpen = mIsMmOpenFromStart;
        if (knownSubset != nullptr) {
            collected = knownSubset->collectedLocked;
            isPortalOpen = isPortalOpen || knownSubset->isPortalOpen;
            for (size_t index = 0; index < mLocked.size(); index++) {
                if (collected[index]) {
                    (mLocked[index].itemGame == GAME_OOT ? ootOwned : mmOwned).push_back(mLocked[index].item);
                }
            }
        }
        while (true) {
            std::vector<std::string> ootSeen;
            std::vector<std::string> mmSeen;
            SharedOwnedViews(mGroups, ootOwned, mmOwned, ootSeen, mmSeen);
            Reach reach;
            const auto oracleStart = std::chrono::steady_clock::now();
            mOracleCalls++;
            if (mIsCrossDungeon) {
                const MmOverworld overworld = ResolveMmOverworld(mIsMmOpenFromStart, mEntryItems, mmSeen);
                auto joint = CrossDungeon::QueryJoint(mInput.ootOracle, mInput.mmOracle, ootSeen, mmSeen, overworld,
                                                      QueryReachable);
                reach.oot = std::move(joint.oot);
                reach.mm = std::move(joint.mm);
            } else {
                reach.oot = QueryReachable(mInput.ootOracle, ootSeen);
                isPortalOpen =
                    isPortalOpen || (mInput.ootOracle.GetPortalOpen != nullptr && OraclePortalOpen(mInput.ootOracle) &&
                                     OwnsMmEntryItems(mEntryItems, mmSeen));
                if (isPortalOpen) {
                    reach.mm = QueryReachable(mInput.mmOracle, mmSeen);
                }
            }
            mOracleMs +=
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - oracleStart)
                    .count();
            MagicMirrorShuffle::AddOotRoute(reach.oot, reach.mm);
            bool hasGrown = false;
            for (size_t index = 0; index < mLocked.size(); index++) {
                const CwPlacedItem& placed = mLocked[index];
                if (collected[index] || !reach.Has(placed.checkGame, placed.check)) {
                    continue;
                }
                collected[index] = true;
                (placed.itemGame == GAME_OOT ? ootOwned : mmOwned).push_back(placed.item);
                hasGrown = true;
            }
            if (hasGrown) {
                continue;
            }
            reach.isGoalMet = IsGoalMet(reach, ootOwned, mmOwned);
            reach.collectedLocked = std::move(collected);
            reach.isPortalOpen = isPortalOpen;
            return reach;
        }
    }

    bool IsGoalMet(const Reach& reach, const std::vector<std::string>& ootOwned,
                   const std::vector<std::string>& mmOwned) const {
        if (!mInput.goalItem.empty()) {
            const auto& owned = mInput.goalItemGame == GAME_OOT ? ootOwned : mmOwned;
            return std::find(owned.begin(), owned.end(), mInput.goalItem) != owned.end();
        }
        if (mInput.goal.hunt) {
            return CountOwnedTriforcePieces(ootOwned, mmOwned) >= mInput.goal.required;
        }
        return IsBossGoalMet(mInput.goal, reach.oot.contains(kGanonCheck), reach.mm.contains(kMajoraCheck));
    }

    const ExportInput& mInput;
    std::vector<CwPlacedItem> mLocked;
    std::vector<CwSharedGroup> mGroups;
    std::vector<std::string> mEntryItems;
    bool mIsMmOpenFromStart;
    bool mIsCrossDungeon;
    std::unordered_map<std::string, Reach> mMemo;
    size_t mQueries = 0;
    size_t mOracleCalls = 0;
    long long mOracleMs = 0;
};

// Item copies a rule asks for, by qualified item name.
using Need = std::map<std::string, int>;

struct Target {
    GameId game = GAME_OOT;
    std::string check;
    bool isGoal = false;

    bool IsMet(const Reach& reach) const {
        return isGoal ? reach.isGoalMet : reach.Has(game, check);
    }
};

inline bool AreAllMet(const std::vector<const Target*>& targets, const Reach& reach) {
    return std::all_of(targets.begin(), targets.end(), [&](const Target* target) { return target->IsMet(reach); });
}

inline std::vector<CwPlacedItem> ExpandNeed(const Need& need, const std::vector<CwPlacedItem>& progression) {
    std::map<std::string, int> left = need;
    std::vector<CwPlacedItem> owned;
    for (const CwPlacedItem& item : progression) {
        int& count = left[QualifiedName(item.itemGame, item.item)];
        if (count > 0) {
            owned.push_back(item);
            count--;
        }
    }
    return owned;
}

// Drops whole runs of candidates while the targets stay reachable, halving the run each pass.
inline std::vector<CwPlacedItem> TrimExtras(Reacher& reacher, const std::vector<const Target*>& targets,
                                            const std::vector<CwPlacedItem>& base,
                                            std::vector<CwPlacedItem> candidates) {
    const Reach& baseReach = reacher.Query(base);
    size_t run = std::max<size_t>(1, candidates.size() / 2);
    while (true) {
        size_t start = 0;
        while (start < candidates.size()) {
            std::vector<CwPlacedItem> trial = base;
            for (size_t index = 0; index < candidates.size(); index++) {
                if (index < start || index >= start + run) {
                    trial.push_back(candidates[index]);
                }
            }
            if (AreAllMet(targets, reacher.Query(trial, &baseReach))) {
                candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(start),
                                 candidates.begin() +
                                     static_cast<std::ptrdiff_t>(std::min(start + run, candidates.size())));
            } else {
                start += run;
            }
        }
        if (run == 1) {
            return candidates;
        }
        run = std::max<size_t>(1, run / 2);
    }
}

// Progression in the order the seed's own playthrough collects it, sphere by sphere.
inline std::vector<CwPlacedItem> PlaythroughOrder(Reacher& reacher, const std::vector<CwPlacedItem>& progression) {
    std::vector<CwPlacedItem> order;
    std::vector<bool> isTaken(progression.size(), false);
    const Reach* previous = nullptr;
    while (order.size() < progression.size()) {
        const Reach& reach = reacher.Query(order, previous);
        previous = &reach;
        const size_t sphereStart = order.size();
        for (size_t index = 0; index < progression.size(); index++) {
            if (!isTaken[index] && reach.Has(progression[index].checkGame, progression[index].check)) {
                isTaken[index] = true;
                order.push_back(progression[index]);
            }
        }
        if (order.size() != sphereStart) {
            continue;
        }
        for (size_t index = 0; index < progression.size(); index++) {
            if (!isTaken[index]) {
                order.push_back(progression[index]);
            }
        }
    }
    return order;
}

// How many items of the playthrough order each target waits for.
inline std::vector<size_t> UnlockPoints(Reacher& reacher, const std::vector<Target>& targets,
                                        const std::vector<CwPlacedItem>& order) {
    std::vector<size_t> unlocks(targets.size(), order.size());
    std::vector<bool> isOpen(targets.size(), false);
    std::vector<CwPlacedItem> prefix;
    const Reach* previous = nullptr;
    for (size_t count = 0; count < order.size(); count++) {
        const Reach& reach = reacher.Query(prefix, previous);
        previous = &reach;
        for (size_t index = 0; index < targets.size(); index++) {
            if (!isOpen[index] && targets[index].IsMet(reach)) {
                isOpen[index] = true;
                unlocks[index] = count;
            }
        }
        prefix.push_back(order[count]);
    }
    return unlocks;
}

inline nlohmann::json RuleJson(const Need& need) {
    nlohmann::json all = nlohmann::json::array();
    for (const auto& [name, count] : need) {
        all.push_back({ { "items", nlohmann::json::array({ name }) }, { "count", count } });
    }
    return nlohmann::json::array({ all });
}

inline nlohmann::json HuntRuleJson(int required) {
    const nlohmann::json pieces = { QualifiedName(GAME_OOT, kOotTriforcePiece),
                                    QualifiedName(GAME_MM, kMmTriforcePiece) };
    return nlohmann::json::array({ nlohmann::json::array({ { { "items", pieces }, { "count", required } } }) });
}

inline bool IsLockedByPolicy(const CwPlacedItem& placed, const FactsByName& facts, const std::string& songs) {
    const auto found = facts.find({ placed.itemGame, placed.item });
    const ItemFacts noFacts;
    const ItemFacts& item = found != facts.end() ? found->second : noFacts;
    const bool isSongHeld = item.song && songs != "anywhere";
    const bool isDungeonBound =
        item.category == "smallKey" || item.category == "bossKey" || item.category == "strayFairy";
    return isSongHeld || isDungeonBound || IsDungeonRewardItem(placed.item);
}

inline std::set<std::pair<GameId, std::string>> FixedChecks(const nlohmann::json& fillSpoiler,
                                                            const std::string& ootDump, const std::string& mmDump) {
    std::set<std::pair<GameId, std::string>> fixed;
    const std::pair<GameId, const std::string*> dumps[] = { { GAME_OOT, &ootDump }, { GAME_MM, &mmDump } };
    for (const auto& [game, text] : dumps) {
        const nlohmann::json dump = nlohmann::json::parse(*text, nullptr, false);
        for (const auto& entry :
             dump.is_object() ? dump.value("fixed", nlohmann::json::array()) : nlohmann::json::array()) {
            fixed.insert({ game, entry.value("check", std::string{}) });
        }
    }
    for (const auto& known : fillSpoiler.value("startKnown", nlohmann::json::array())) {
        fixed.insert({ known.value("checkGame", "oot") == "mm" ? GAME_MM : GAME_OOT, known.value("checkName", "") });
    }
    return fixed;
}

inline nlohmann::json NameTables(const std::string& ootDump, const std::string& mmDump) {
    nlohmann::json items = nlohmann::json::object();
    nlohmann::json locations = nlohmann::json::object();
    const std::pair<GameId, const std::string*> dumps[] = { { GAME_OOT, &ootDump }, { GAME_MM, &mmDump } };
    for (const auto& [game, text] : dumps) {
        const nlohmann::json dump = nlohmann::json::parse(*text, nullptr, false);
        if (!dump.is_object()) {
            continue;
        }
        for (const auto& check : dump.value("checks", nlohmann::json::array())) {
            const std::string name = QualifiedName(game, check.value("name", std::string{}));
            locations[name] = NameId(name);
        }
        for (const char* section : { "items", "pool" }) {
            for (const auto& item : dump.value(section, nlohmann::json::array())) {
                const std::string name = QualifiedName(game, item.value("name", std::string{}));
                items[name] = NameId(name);
            }
        }
        for (const auto& item : dump.value("fixed", nlohmann::json::array())) {
            const std::string name = QualifiedName(game, item.value("item", std::string{}));
            items[name] = NameId(name);
        }
        for (const auto& junk : dump.value("junkPool", nlohmann::json::array())) {
            const std::string name = QualifiedName(game, junk.get<std::string>());
            items[name] = NameId(name);
        }
    }
    return { { "items", std::move(items) }, { "locations", std::move(locations) } };
}

inline std::string FindIdCollision(const nlohmann::json& table) {
    std::map<int64_t, std::string> owners;
    for (const auto& [name, id] : table.items()) {
        const auto [owner, isNew] = owners.emplace(id.get<int64_t>(), name);
        if (!isNew) {
            return owner->second + " / " + name;
        }
    }
    return {};
}

inline std::string Classify(const CwPlacedItem& placed, const ItemFacts& facts) {
    if (placed.advancement || facts.advancement) {
        return "progression";
    }
    if (placed.item == "Ice Trap" || placed.item == "Knockoff Item") {
        return "trap";
    }
    return placed.major || facts.major ? "useful" : "filler";
}

struct ExportResult {
    nlohmann::json seedData;
    nlohmann::json localMap;
    nlohmann::json names;
    std::string error;
};

inline ExportResult BuildExport(const ExportInput& input) {
    ExportResult result;
    const nlohmann::json fill = nlohmann::json::parse(input.fillSpoiler, nullptr, false);
    if (!fill.is_object()) {
        result.error = "the fill left no readable placements";
        return result;
    }
    const FactsByName facts = ReadItemFacts(input.ootDump, input.mmDump);
    const auto fixedChecks = FixedChecks(fill, input.ootDump, input.mmDump);
    std::vector<CwPlacedItem> locked;
    std::vector<CwPlacedItem> shuffled;
    for (CwPlacedItem placed : ParseSpoilerPlacements(input.fillSpoiler, input.ootDump, input.mmDump)) {
        placed.advancement =
            placed.advancement || (placed.itemGame == input.goalItemGame && placed.item == input.goalItem);
        const bool isLocked =
            fixedChecks.contains({ placed.checkGame, placed.check }) || IsLockedByPolicy(placed, facts, input.songs);
        (isLocked ? locked : shuffled).push_back(placed);
    }

    // A check no item set can reach keeps its item; dropping that item can shrink what the rest reach, so repeat.
    std::vector<CwPlacedItem> progression;
    while (true) {
        progression.clear();
        for (const CwPlacedItem& placed : shuffled) {
            if (placed.advancement) {
                progression.push_back(placed);
            }
        }
        Reacher reacher(input, locked);
        const Reach& everything = reacher.Query(progression);
        const auto unreachable = std::stable_partition(shuffled.begin(), shuffled.end(), [&](const CwPlacedItem& p) {
            return everything.Has(p.checkGame, p.check);
        });
        if (unreachable == shuffled.end()) {
            if (!everything.isGoalMet) {
                result.error = "the seed's goal cannot be reached with every item";
                return result;
            }
            break;
        }
        locked.insert(locked.end(), unreachable, shuffled.end());
        shuffled.erase(unreachable, shuffled.end());
    }

    std::vector<Target> targets;
    for (const CwPlacedItem& placed : shuffled) {
        targets.push_back({ placed.checkGame, placed.check, false });
    }
    targets.push_back({ GAME_OOT, {}, true });

    Reacher reacher(input, locked);
    const Reach& fromScratch = reacher.Query({});
    std::map<std::string, int> copies;
    for (const CwPlacedItem& item : progression) {
        copies[QualifiedName(item.itemGame, item.item)]++;
    }
    std::vector<Need> needs(targets.size());
    for (const auto& [name, count] : copies) {
        for (int kept = 0; kept < count; kept++) {
            std::vector<CwPlacedItem> owned;
            int left = kept;
            for (const CwPlacedItem& item : progression) {
                if (QualifiedName(item.itemGame, item.item) != name) {
                    owned.push_back(item);
                } else if (left > 0) {
                    owned.push_back(item);
                    left--;
                }
            }
            const Reach& reach = reacher.Query(owned, &fromScratch);
            bool isAnyLost = false;
            for (size_t index = 0; index < targets.size(); index++) {
                if (!targets[index].IsMet(reach)) {
                    needs[index][name] = kept + 1;
                    isAnyLost = true;
                }
            }
            if (!isAnyLost) {
                break;
            }
        }
    }
    std::cout << "[fleet-archipelago] necessary items found after " << reacher.DescribeCost() << std::endl;

    const std::vector<CwPlacedItem> order = PlaythroughOrder(reacher, progression);
    const std::vector<size_t> unlocks = UnlockPoints(reacher, targets, order);
    std::cout << "[fleet-archipelago] unlock points found after " << reacher.DescribeCost() << std::endl;
    // Grouping also by unlock point keeps an early check from inheriting a later check's rule.
    std::map<std::pair<Need, size_t>, std::vector<size_t>> groups;
    for (size_t index = 0; index < targets.size(); index++) {
        groups[{ needs[index], unlocks[index] }].push_back(index);
    }
    std::vector<nlohmann::json> rules(targets.size());
    for (const auto& [key, members] : groups) {
        const auto& [need, unlock] = key;
        std::vector<const Target*> group;
        for (size_t index : members) {
            group.push_back(&targets[index]);
        }
        const std::vector<CwPlacedItem> base = ExpandNeed(need, progression);
        Need full = need;
        if (!AreAllMet(group, reacher.Query(base, &fromScratch))) {
            std::map<std::string, int> used = need;
            std::vector<CwPlacedItem> candidates;
            for (size_t step = 0; step < unlock; step++) {
                const CwPlacedItem& item = order[step];
                int& count = used[QualifiedName(item.itemGame, item.item)];
                if (count > 0) {
                    count--;
                } else {
                    candidates.push_back(item);
                }
            }
            for (const CwPlacedItem& extra : TrimExtras(reacher, group, base, std::move(candidates))) {
                full[QualifiedName(extra.itemGame, extra.item)]++;
            }
        }
        for (size_t index : members) {
            rules[index] = RuleJson(full);
        }
    }
    std::cout << "[fleet-archipelago] rules for " << groups.size() << " groups derived after " << reacher.DescribeCost()
              << std::endl;

    nlohmann::json locations = nlohmann::json::array();
    nlohmann::json localLocations = nlohmann::json::object();
    for (size_t index = 0; index + 1 < targets.size(); index++) {
        const std::string name = QualifiedName(targets[index].game, targets[index].check);
        locations.push_back({ { "name", name }, { "rule", rules[index] } });
        localLocations[name] = { { "game", GameKey(targets[index].game) }, { "check", targets[index].check } };
    }
    std::map<std::string, nlohmann::json> pool;
    nlohmann::json localItems = nlohmann::json::object();
    std::map<std::string, int> fillerCounts;
    for (const CwPlacedItem& placed : shuffled) {
        const std::string name = QualifiedName(placed.itemGame, placed.item);
        const auto found = facts.find({ placed.itemGame, placed.item });
        const ItemFacts itemFacts = found != facts.end() ? found->second : ItemFacts{};
        nlohmann::json& entry = pool[name];
        if (entry.is_null()) {
            entry = { { "name", name }, { "count", 0 }, { "classification", Classify(placed, itemFacts) } };
        }
        entry["count"] = entry["count"].get<int>() + 1;
        localItems[name] = { { "game", GameKey(placed.itemGame) },
                             { "item", placed.item },
                             { "advancement", placed.advancement },
                             { "category", itemFacts.category } };
        if (entry["classification"] == "filler") {
            fillerCounts[name]++;
        }
    }
    nlohmann::json items = nlohmann::json::array();
    for (auto& [name, entry] : pool) {
        items.push_back(std::move(entry));
    }
    const auto commonest = std::max_element(fillerCounts.begin(), fillerCounts.end(),
                                            [](const auto& a, const auto& b) { return a.second < b.second; });
    const std::string filler = commonest != fillerCounts.end()
                                   ? commonest->first
                                   : QualifiedName(input.mmStart ? GAME_MM : GAME_OOT, "Green Rupee");
    const bool isHuntGoal = input.goal.hunt && input.goalItem.empty();
    const nlohmann::json goal = isHuntGoal ? HuntRuleJson(input.goal.required) : rules.back();

    result.names = NameTables(input.ootDump, input.mmDump);
    for (const auto& [name, entry] : localItems.items()) {
        result.names["items"][name] = NameId(name);
    }
    for (const auto& [name, entry] : localLocations.items()) {
        result.names["locations"][name] = NameId(name);
    }
    result.names["items"][filler] = NameId(filler);
    for (const char* table : { "items", "locations" }) {
        if (const std::string clash = FindIdCollision(result.names[table]); !clash.empty()) {
            result.error = "two Archipelago names share an id: " + clash;
            return result;
        }
    }
    result.seedData = { { "format", kExportFormat },   { "exportId", input.exportId },
                        { "mode", input.mode },        { "locations", std::move(locations) },
                        { "items", std::move(items) }, { "goal", goal },
                        { "filler", filler } };
    result.localMap = { { "exportId", input.exportId },
                        { "mode", input.mode },
                        { "locations", std::move(localLocations) },
                        { "items", std::move(localItems) } };
    return result;
}

struct ScoutedItem {
    std::string location;
    int64_t item = 0;
    int player = 0;
    int flags = 0;
};

// Rewrites the fill's placements with the multiworld's choices; items for other players become this mod's keys.
inline nlohmann::json ApplyMultiworld(const nlohmann::json& fillSpoiler, const nlohmann::json& localMap,
                                      const std::vector<ScoutedItem>& scouted, int ownSlot,
                                      const std::map<int64_t, std::string>& ownItemNames, const std::string& mmDump) {
    nlohmann::json spoiler = fillSpoiler;
    const nlohmann::json& locations = localMap.at("locations");
    const nlohmann::json& items = localMap.at("items");
    std::set<std::pair<std::string, std::string>> multiworldChecks;
    for (const auto& [name, location] : locations.items()) {
        multiworldChecks.emplace(location.at("game").get<std::string>(), location.at("check").get<std::string>());
    }
    nlohmann::json foreign = nlohmann::json::array();
    for (const auto& marker : spoiler.value("foreign", nlohmann::json::array())) {
        if (!multiworldChecks.contains({ marker.value("checkGame", ""), marker.value("checkName", "") })) {
            foreign.push_back(marker);
        }
    }
    std::vector<std::string> junkNames;
    const nlohmann::json mm = nlohmann::json::parse(mmDump, nullptr, false);
    for (const auto& junk : mm.is_object() ? mm.value("junkPool", nlohmann::json::array()) : nlohmann::json::array()) {
        junkNames.push_back(junk.get<std::string>());
    }
    std::sort(junkNames.begin(), junkNames.end());
    junkNames.erase(std::unique(junkNames.begin(), junkNames.end()), junkNames.end());
    const uint32_t masterSeed = spoiler.value("masterSeed", 0u);

    for (const ScoutedItem& found : scouted) {
        const auto location = locations.find(found.location);
        if (location == locations.end()) {
            throw std::runtime_error("the multiworld names a location this export lacks: " + found.location);
        }
        const std::string checkGame = location->at("game");
        const std::string check = location->at("check");
        if (found.player != ownSlot) {
            spoiler[checkGame][check] = ForeignKey(found.player, found.item);
            continue;
        }
        const auto name = ownItemNames.find(found.item);
        if (name == ownItemNames.end() || !items.contains(name->second)) {
            throw std::runtime_error("the multiworld placed an item this export lacks at " + found.location);
        }
        const nlohmann::json& item = items.at(name->second);
        const std::string itemGame = item.at("game");
        std::string itemName = item.at("item");
        // MM resolves its junk placeholder at pickup from the MM check, which an OoT check lacks.
        if (checkGame == "oot" && itemGame == "mm" && itemName == kMmJunkName && !junkNames.empty()) {
            CwRng junk((static_cast<uint64_t>(masterSeed) << 32) ^ CwHashName(check) ^ 0x4A554E4BULL);
            itemName = junkNames[junk.below(static_cast<uint32_t>(junkNames.size()))];
        }
        spoiler[checkGame][check] = itemName;
        if (itemGame != checkGame) {
            nlohmann::json marker = { { "checkGame", checkGame },
                                      { "checkName", check },
                                      { "itemGame", itemGame },
                                      { "itemName", itemName },
                                      { "advancement", item.value("advancement", false) } };
            if (!item.value("category", std::string{}).empty()) {
                marker["category"] = item.at("category");
            }
            foreign.push_back(std::move(marker));
        }
    }
    spoiler["foreign"] = std::move(foreign);
    spoiler["ootCount"] = spoiler.value("oot", nlohmann::json::object()).size();
    spoiler["mmCount"] = spoiler.value("mm", nlohmann::json::object()).size();
    return spoiler;
}

} // namespace ComboRando::Archipelago
