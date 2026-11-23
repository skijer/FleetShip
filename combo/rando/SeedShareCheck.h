#pragma once

#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "CrossForeign.h"
#include "SharedItems.h"

// Reads a generated Fleet seed and checks its sharing against what the generator was asked for: the families and
// mod pairs, the pairs today's mods are expected to form, and the copies the fill kept of each shared item.
namespace ComboRando::SeedShareCheck {

using CopyKey = std::pair<GameId, std::string>;
using CopyCounts = std::map<CopyKey, size_t>;

struct Expected {
    uint64_t sharedMask = 0;
    std::set<std::string> unsharedPairs;
    nlohmann::json baselinePairs = nlohmann::json::array();
};

struct Report {
    std::vector<std::string> failures;
    nlohmann::json summary = nlohmann::json::object();

    void Fail(const std::string& what) {
        failures.push_back(what);
    }
};

inline bool IsHunt(const nlohmann::json& seed) {
    return seed.value("goal", nlohmann::json::object()).value("type", std::string{}) == "triforceHunt";
}

inline std::vector<CwSharedPair> ReadModPairs(const nlohmann::json& seed) {
    std::vector<CwSharedPair> pairs;
    for (const auto& pair : seed.value("modFamilies", nlohmann::json::array())) {
        pairs.push_back({ pair.value("oot", std::string{}), pair.value("mm", std::string{}),
                          pair.value("key", std::string{}) });
    }
    return pairs;
}

inline std::set<std::string> ReadKeys(const nlohmann::json& list, const char* field = nullptr) {
    std::set<std::string> keys;
    for (const auto& entry : list) {
        if (field != nullptr && entry.is_object()) {
            keys.insert(entry.value(field, std::string{}));
        } else if (entry.is_string()) {
            keys.insert(entry.get<std::string>());
        }
    }
    return keys;
}

inline std::string JoinKeys(const std::set<std::string>& keys) {
    std::string joined;
    for (const auto& key : keys) {
        joined += (joined.empty() ? "" : ", ") + key;
    }
    return joined;
}

// Every placed copy, credited to the game that owns the item; starting items left the pool and are not counted.
inline CopyCounts CountPlacedCopies(const nlohmann::json& seed) {
    std::map<std::pair<std::string, std::string>, CopyKey> foreignByCheck;
    for (const auto& foreign : seed.value("foreign", nlohmann::json::array())) {
        foreignByCheck[{ foreign.value("checkGame", std::string{}), foreign.value("checkName", std::string{}) }] = {
            KeyToGameId(foreign.value("itemGame", std::string{})), foreign.value("itemName", std::string{})
        };
    }
    CopyCounts counts;
    for (const char* game : { "oot", "mm" }) {
        const nlohmann::json placements =
            seed.value(game, nlohmann::json::object()).value("placements", nlohmann::json::object());
        for (const auto& [check, item] : placements.items()) {
            const auto foreign = foreignByCheck.find({ game, check });
            const CopyKey key = foreign != foreignByCheck.end()
                                    ? foreign->second
                                    : CopyKey{ KeyToGameId(game), StripGameSuffix(item.get<std::string>()) };
            counts[key]++;
        }
    }
    return counts;
}

inline size_t CopiesOf(const CopyCounts& counts, GameId game, const std::string& name) {
    const auto found = counts.find({ game, name });
    return found == counts.end() ? 0 : found->second;
}

// The fill leaves a requested family out when a pool lacks its item, or when it is a mask and Mask Quest is off.
inline std::string ExplainDroppedFamily(int family, const CopyCounts& pool) {
    const SharedFamilyDef& def = SharedFamilyByIndex(family);
    if (def.isMask) {
        return "mask family, shared only with Mask Quest shuffle";
    }
    if (family != SF_BOTTLES && CopiesOf(pool, GAME_OOT, def.ootName) == 0) {
        return std::string("the OoT pool has no ") + def.ootName;
    }
    if (def.mmHasItem && CopiesOf(pool, GAME_MM, def.mmName) == 0 && FindTierLadder(family) == nullptr) {
        return std::string("the MM pool has no ") + def.mmName;
    }
    return "";
}

inline void CheckSharedFamilies(const nlohmann::json& seed, const Expected& expected, const CopyCounts& pool,
                                Report& report) {
    const uint64_t seedMask = SharedMaskFromKeys(seed.value("sharedItems", nlohmann::json::array()));
    nlohmann::json dropped = nlohmann::json::array();
    for (int family = 0; family < SF_COUNT; family++) {
        const uint64_t bit = uint64_t{ 1 } << family;
        const std::string key = SharedFamilyByIndex(family).key;
        if ((seedMask & bit) != 0 && (expected.sharedMask & bit) == 0) {
            report.Fail("family " + key + " is shared but was not chosen");
        } else if ((seedMask & bit) == 0 && (expected.sharedMask & bit) != 0) {
            const std::string reason = ExplainDroppedFamily(family, pool);
            if (reason.empty()) {
                report.Fail("family " + key + " was chosen but the seed does not share it");
            } else {
                dropped.push_back({ { "family", key }, { "reason", reason } });
            }
        }
    }
    report.summary["sharedFamilies"] = SharedKeysFromMask(seedMask).size();
    report.summary["droppedFamilies"] = dropped;
}

inline void CheckUnsharedPairs(const nlohmann::json& seed, const Expected& expected, Report& report) {
    std::set<std::string> recorded = ReadKeys(seed.value("unsharedPairs", nlohmann::json::array()));
    std::set<std::string> chosen = expected.unsharedPairs;
    if (IsHunt(seed)) {
        recorded.erase(kTriforcePairKey);
        chosen.erase(kTriforcePairKey);
    }
    std::set<std::string> extra;
    std::set<std::string> missing;
    std::set_difference(recorded.begin(), recorded.end(), chosen.begin(), chosen.end(),
                        std::inserter(extra, extra.end()));
    std::set_difference(chosen.begin(), chosen.end(), recorded.begin(), recorded.end(),
                        std::inserter(missing, missing.end()));
    if (!extra.empty()) {
        report.Fail("the seed unshares pairs that were not chosen: " + JoinKeys(extra));
    }
    if (!missing.empty()) {
        report.Fail("the seed lost unshared choices: " + JoinKeys(missing));
    }
    for (const CwSharedPair& pair : ReadModPairs(seed)) {
        if (expected.unsharedPairs.contains(pair.key)) {
            report.Fail("pair " + pair.key + " crosses games although it was unshared");
        }
    }
}

// The baseline lists the pairs today's mods form; a missing one means a mod is absent or stopped pairing.
inline void CheckBaseline(const nlohmann::json& seed, const Expected& expected, Report& report) {
    const std::set<std::string> formed = ReadKeys(seed.value("modFamilies", nlohmann::json::array()), "key");
    const std::set<std::string> baseline = ReadKeys(expected.baselinePairs, "key");
    std::set<std::string> missing;
    for (const std::string& key : baseline) {
        const bool isHuntPiece = IsHunt(seed) && key == kTriforcePairKey;
        if (!formed.contains(key) && !expected.unsharedPairs.contains(key) && !isHuntPiece) {
            missing.insert(key);
        }
    }
    std::set<std::string> added;
    std::set_difference(formed.begin(), formed.end(), baseline.begin(), baseline.end(),
                        std::inserter(added, added.end()));
    // The goal shares Triforce pieces itself; the pair has no row in the launcher.
    added.erase(kTriforcePairKey);
    if (!missing.empty()) {
        report.Fail(std::to_string(missing.size()) + " expected mod pairs did not form: " + JoinKeys(missing));
    }
    report.summary["modPairs"] = formed.size();
    report.summary["newModPairs"] = added;
}

inline bool IsCountedByCopies(const CwSharedGroup& group, bool plentifulModItems) {
    if (!group.ladders[0].empty() || !group.ladders[1].empty() || (plentifulModItems && !group.nativeFamily)) {
        return false;
    }
    return std::none_of(group.names.begin(), group.names.end(), [](const CwSharedName& name) {
        const bool isBottle = FindBottleTwin(name.game, name.name) != nullptr;
        return isBottle || FindNativePairKey(name.game, name.name) == kTriforcePairKey;
    });
}

// Mirrors CrossWorldCombinedFill: a shared group keeps as many copies as the game that counts holds alone.
inline size_t ExpectedSharedCopies(const CwSharedGroup& group, const CopyCounts& pool) {
    size_t target = 0;
    size_t available = 0;
    std::array<size_t, 2> tierCopies = {};
    for (const CwSharedName& name : group.names) {
        const size_t copies = CopiesOf(pool, name.game, name.name);
        available += copies;
        if (IsTierAliasName(name.game, name.name)) {
            tierCopies[name.game] += copies;
            target = std::max(target, tierCopies[name.game]);
        }
        if (group.nei || name.game == GAME_OOT) {
            target = std::max(target, copies);
        } else if (group.countsMmTiers) {
            target = std::max(target, std::min(copies, group.mmTierCap));
        }
    }
    return std::min(target, available);
}

inline std::string DescribeGroup(const CwSharedGroup& group) {
    std::string names;
    for (const CwSharedName& name : group.names) {
        names += (names.empty() ? "" : " / ") + name.name + (name.game == GAME_OOT ? " (OoT)" : " (MM)");
    }
    return names;
}

// Compares against the same seed number generated with nothing shared, which holds every game's own copies.
inline void CheckCopies(const nlohmann::json& seed, const nlohmann::json& unsharedSeed, const Expected& expected,
                        Report& report) {
    const CopyCounts placed = CountPlacedCopies(seed);
    const CopyCounts pool = CountPlacedCopies(unsharedSeed);
    const uint64_t mask =
        SharedMaskFromKeys(seed.value("sharedItems", nlohmann::json::array())) & ~SharedLiveStateMask();
    size_t checkedGroups = 0;
    for (const CwSharedGroup& group : BuildSharedItemGroups(ReadModPairs(seed), mask)) {
        if (!IsCountedByCopies(group, seed.value("plentifulModItems", false))) {
            continue;
        }
        checkedGroups++;
        size_t actual = 0;
        for (const CwSharedName& name : group.names) {
            actual += CopiesOf(placed, name.game, name.name);
        }
        const size_t wanted = ExpectedSharedCopies(group, pool);
        if (actual != wanted) {
            report.Fail("shared " + DescribeGroup(group) + ": " + std::to_string(actual) + " copies, expected " +
                        std::to_string(wanted));
        }
    }
    size_t checkedPairs = 0;
    for (const auto& pair : expected.baselinePairs) {
        if (!expected.unsharedPairs.contains(pair.value("key", std::string{}))) {
            continue;
        }
        checkedPairs++;
        for (const auto& [game, field] : { std::pair{ GAME_OOT, "oot" }, std::pair{ GAME_MM, "mm" } }) {
            const std::string name = pair.value(field, std::string{});
            const size_t own = CopiesOf(placed, game, name);
            const size_t alone = CopiesOf(pool, game, name);
            if (own != alone) {
                report.Fail("unshared " + name + " (" + field + "): " + std::to_string(own) + " copies, expected " +
                            std::to_string(alone));
            }
        }
    }
    report.summary["copyGroupsChecked"] = checkedGroups;
    report.summary["unsharedPairsChecked"] = checkedPairs;
}

inline Report CheckSeed(const nlohmann::json& seed, const nlohmann::json& unsharedSeed, const Expected& expected) {
    Report report;
    if (seed.value("fileType", std::string{}) != "ComboShipRandomizer") {
        report.Fail("not a Fleet seed");
        return report;
    }
    const bool hasReference = unsharedSeed.is_object();
    const CopyCounts pool = CountPlacedCopies(hasReference ? unsharedSeed : seed);
    report.summary["seed"] = seed.value("seed", 0);
    report.summary["placements"] = { { "oot", seed["oot"].value("placements", nlohmann::json::object()).size() },
                                     { "mm", seed["mm"].value("placements", nlohmann::json::object()).size() },
                                     { "acrossGames", seed.value("foreign", nlohmann::json::array()).size() } };
    CheckSharedFamilies(seed, expected, pool, report);
    CheckUnsharedPairs(seed, expected, report);
    if (!expected.baselinePairs.empty()) {
        CheckBaseline(seed, expected, report);
    }
    if (hasReference) {
        CheckCopies(seed, unsharedSeed, expected, report);
    }
    return report;
}

} // namespace ComboRando::SeedShareCheck
