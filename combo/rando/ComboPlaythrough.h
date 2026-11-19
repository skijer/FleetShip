#pragma once

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CrossWorldRando.h"
#include "SharedItems.h"

namespace ComboRando {

inline uint64_t SharedMaskForSpoiler(const std::string& spoilerJson, uint64_t fallback) {
    try {
        auto j = nlohmann::json::parse(spoilerJson);
        if (j.contains("sharedItems"))
            return SharedMaskFromKeys(j["sharedItems"]);
    } catch (...) {}
    return fallback;
}

struct CwPlacedItem {
    GameId checkGame;
    std::string check;
    GameId itemGame;
    std::string item;
    bool advancement;
    bool major;
};

inline std::vector<CwPlacedItem> ParseSpoilerPlacements(const std::string& spoilerJson,
                                                        const std::string& sohDumpJson = "",
                                                        const std::string& mmDumpJson = "") {
    std::vector<CwPlacedItem> placements;
    struct ForeignInfo {
        GameId itemGame;
        std::string itemName;
        bool advancement;
    };
    std::unordered_map<std::string, ForeignInfo> foreign;

    std::unordered_map<std::string, bool> advByName[2];
    std::unordered_map<std::string, bool> majorByName[2];
    auto loadAdv = [&](GameId g, const std::string& dumpJson) {
        if (dumpJson.empty())
            return;
        try {
            auto d = nlohmann::json::parse(dumpJson);
            for (auto& it : d.value("pool", nlohmann::json::array())) {
                bool adv = it.value("advancement", true);
                advByName[g][it.value("name", "")] |= adv;
                majorByName[g][it.value("name", "")] |= it.value("major", adv);
            }
            for (auto& f : d.value("fixed", nlohmann::json::array())) {
                bool adv = f.value("advancement", true);
                advByName[g][f.value("item", "")] |= adv;
                majorByName[g][f.value("item", "")] |= f.value("major", adv);
            }
        } catch (...) {}
    };
    loadAdv(GAME_OOT, sohDumpJson);
    loadAdv(GAME_MM, mmDumpJson);
    auto lookupAdv = [&](GameId g, const std::string& item) {
        auto it = advByName[g].find(item);
        return it == advByName[g].end() ? true : it->second;
    };
    auto lookupMajor = [&](GameId g, const std::string& item) {
        auto it = majorByName[g].find(item);
        return it == majorByName[g].end() ? lookupAdv(g, item) : it->second;
    };
    auto stripNativeSuffix = [](std::string s, GameId game) {
        const std::string suffix = GameSuffix(game);
        if (s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0)
            return s.substr(0, s.size() - suffix.size());
        return s;
    };
    try {
        auto j = nlohmann::json::parse(spoilerJson);
        for (auto& fm : j.value("foreign", nlohmann::json::array())) {
            std::string cg = fm.value("checkGame", ""), cn = fm.value("checkName", "");
            std::string ig = fm.value("itemGame", "");
            foreign[cg + ":" + cn] = { (ig == "mm") ? GAME_MM : GAME_OOT, fm.value("itemName", ""),
                                       fm.value("advancement", true) };
        }
        auto addGame = [&](const char* key, GameId cg) {
            if (!j.contains(key) || !j[key].is_object())
                return;
            for (auto& [cn, iv] : j[key].items()) {
                auto fit = foreign.find(std::string(key) + ":" + cn);
                bool isForeign = fit != foreign.end();
                GameId ig = isForeign ? fit->second.itemGame : cg;
                std::string item = (isForeign && !fit->second.itemName.empty())
                                       ? fit->second.itemName
                                       : stripNativeSuffix(iv.get<std::string>(), cg);
                bool adv = isForeign ? fit->second.advancement : lookupAdv(ig, item);
                bool major = isForeign ? fit->second.advancement : lookupMajor(ig, item);
                placements.push_back({ cg, cn, ig, item, adv, major });
            }
        };
        addGame("oot", GAME_OOT);
        addGame("mm", GAME_MM);
    } catch (const std::exception& e) { std::cerr << "[PLAYTHROUGH] spoiler parse error: " << e.what() << "\n"; }
    return placements;
}

inline std::unordered_set<std::string> QueryReachable(const OracleFns& o, const std::vector<std::string>& owned) {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& n : owned)
        arr.push_back(n);
    o.Reset();
    o.SetOwnedItems(arr.dump().c_str());
    std::unordered_set<std::string> out;
    try {
        for (auto& n : nlohmann::json::parse(o.GetReachableChecks()))
            out.insert(n.get<std::string>());
    } catch (...) {}
    return out;
}

inline bool OraclePortalOpen(const OracleFns& o) {
    return o.GetPortalOpen == nullptr || o.GetPortalOpen() != 0;
}

using ReachSet = std::shared_ptr<const std::unordered_set<std::string>>;

struct ReachResult {
    ReachSet reach;
    bool portalOpen = true;
};

// Sort only: deduplicating would collapse progressive item counts into a stale cache hit.
inline std::string CanonicalOwnedKey(std::vector<std::string> owned) {
    std::sort(owned.begin(), owned.end());
    std::string key;
    for (auto& n : owned) {
        key += n;
        key += '\x1f';
    }
    return key;
}

inline ReachResult QueryReachableMemo(const OracleFns& o, const std::vector<std::string>& owned,
                                      std::unordered_map<std::string, ReachResult>& memo) {
    std::string key = CanonicalOwnedKey(owned);
    auto it = memo.find(key);
    if (it != memo.end())
        return it->second;
    // The portal bit is cached with the set: a memo hit runs no search, so a later read would be stale.
    ReachResult r{ std::make_shared<const std::unordered_set<std::string>>(QueryReachable(o, owned)), false };
    r.portalOpen = OraclePortalOpen(o);
    memo.emplace(std::move(key), r);
    return r;
}

inline std::string CanonicalJointOwnedKey(std::vector<std::string> oot, std::vector<std::string> mm) {
    std::string key;
    auto append = [&](auto& owned) {
        std::sort(owned.begin(), owned.end());
        key += std::to_string(owned.size()) + ":";
        for (const auto& name : owned)
            key += std::to_string(name.size()) + ":" + name;
    };
    append(oot);
    append(mm);
    return key;
}

struct JointReachResult {
    ReachSet oot, mm;
};

class JointReachMemo {
  public:
    JointReachMemo(const OracleFns& oot, const OracleFns& mm, bool mmInitiallyOpen,
                   std::vector<std::string> mmEntryItems)
        : oot(oot), mm(mm), mmInitiallyOpen(mmInitiallyOpen), mmEntryItems(std::move(mmEntryItems)) {
    }

    JointReachResult Query(const std::vector<std::string>& ootOwned, const std::vector<std::string>& mmOwned) {
        auto key = CanonicalJointOwnedKey(ootOwned, mmOwned);
        const auto found = memo.find(key);
        if (found != memo.end()) {
            return found->second;
        }
        const MmOverworld mmOverworld = ResolveMmOverworld(mmInitiallyOpen, mmEntryItems, mmOwned);
        auto reach = CrossDungeon::QueryJoint(oot, mm, ootOwned, mmOwned, mmOverworld, QueryReachable);
        JointReachResult result{ std::make_shared<const std::unordered_set<std::string>>(std::move(reach.oot)),
                                 std::make_shared<const std::unordered_set<std::string>>(std::move(reach.mm)) };
        memo.emplace(std::move(key), result);
        return result;
    }

  private:
    const OracleFns oot, mm;
    const bool mmInitiallyOpen;
    const std::vector<std::string> mmEntryItems;
    std::unordered_map<std::string, JointReachResult> memo;
};

using GoalPredicate =
    std::function<bool(const std::unordered_set<std::string>& ootReach, const std::unordered_set<std::string>& mmReach,
                       const std::vector<std::string>& ownedOot, const std::vector<std::string>& ownedMm)>;
inline bool DefaultGanonMajoraGoal(const std::unordered_set<std::string>& ootReach,
                                   const std::unordered_set<std::string>& mmReach,
                                   const std::vector<std::string>& /*ownedOot*/,
                                   const std::vector<std::string>& /*ownedMm*/) {
    static const char* kOotGanon = "Ganon";
    static const char* kMmWin = "Moon Majora Pot 01";
    return ootReach.count(kOotGanon) > 0 && mmReach.count(kMmWin) > 0;
}

inline bool EitherGanonMajoraGoal(const std::unordered_set<std::string>& ootReach,
                                  const std::unordered_set<std::string>& mmReach,
                                  const std::vector<std::string>& /*ownedOot*/,
                                  const std::vector<std::string>& /*ownedMm*/) {
    return ootReach.count("Ganon") > 0 || mmReach.count("Moon Majora Pot 01") > 0;
}

inline bool MmOnlyMajoraGoal(const std::unordered_set<std::string>& /*ootReach*/,
                             const std::unordered_set<std::string>& mmReach,
                             const std::vector<std::string>& /*ownedOot*/,
                             const std::vector<std::string>& /*ownedMm*/) {
    static const char* kMmWin = "Moon Majora Pot 01";
    return mmReach.count(kMmWin) > 0;
}

inline int CountOwnedTriforcePieces(const std::vector<std::string>& ownedOot, const std::vector<std::string>& ownedMm) {
    int n = 0;
    for (const auto& i : ownedOot)
        n += (i == kOotTriforcePiece) ? 1 : 0;
    for (const auto& i : ownedMm)
        n += (i == kMmTriforcePiece) ? 1 : 0;
    return n;
}

inline GoalPredicate MakeTriforceHuntGoal(int required) {
    return [required](const std::unordered_set<std::string>&, const std::unordered_set<std::string>&,
                      const std::vector<std::string>& ownedOot, const std::vector<std::string>& ownedMm) {
        return CountOwnedTriforcePieces(ownedOot, ownedMm) >= required;
    };
}

struct RequirednessResult {
    std::unordered_map<std::string, bool> requiredByCheck;
    std::unordered_map<std::string, bool> areaHasRequired;
    int candidateCount = 0;
    int replayedCount = 0;
    int64_t ms = 0;
};

inline RequirednessResult PareDownPlaythrough(const std::string& spoilerJson, const OracleFns& ootOracle,
                                              const OracleFns& mmOracle, void (*mmRestore)(),
                                              const std::string& sohDumpJson = "", const std::string& mmDumpJson = "",
                                              const std::unordered_map<std::string, std::string>& ootCheckAreas = {},
                                              const std::unordered_map<std::string, std::string>& mmCheckAreas = {},
                                              GoalPredicate goalReached = DefaultGanonMajoraGoal,
                                              bool portalGated = true, bool mmStart = false,
                                              ComboGenProgress* progress = nullptr, uint64_t sharedMask = 0,
                                              const std::string& sharedPairsJson = "") {
    RequirednessResult result;
    auto placements = ParseSpoilerPlacements(spoilerJson, sohDumpJson, mmDumpJson);
    const auto sharedGroups = BuildSharedItemGroups(ResolveSharedPairs(sharedPairsJson, mmDumpJson, sohDumpJson),
                                                    SharedMaskForSpoiler(spoilerJson, sharedMask));

    auto checkKey = [](const CwPlacedItem& p) {
        return std::string(p.checkGame == GAME_OOT ? "oot:" : "mm:") + p.check;
    };

    std::unordered_map<std::string, ReachResult> ootMemo, mmMemo;
    const std::vector<std::string> mmEntryItems = MmEntryItems(mmDumpJson);
    JointReachMemo jointMemo(ootOracle, mmOracle, !portalGated || mmStart, mmEntryItems);
    static const auto kEmptyReach = std::make_shared<const std::unordered_set<std::string>>();

    struct ReplayState {
        std::vector<std::string> ootOwned, mmOwned;
        std::vector<char> credited;
        bool portalOpen = false;
    };
    constexpr size_t kExcludeNone = SIZE_MAX;

    auto replay = [&](ReplayState state, size_t excluded, std::vector<ReplayState>* spheres,
                      std::vector<int>* sphereOf) {
        std::vector<std::string> ootSeen, mmSeen;
        for (int sphere = 0;; ++sphere) {
            if (spheres)
                spheres->push_back(state);
            SharedOwnedViews(sharedGroups, state.ootOwned, state.mmOwned, ootSeen, mmSeen);
            ReachSet ootReach, mmReach;
            if (ootOracle.SetDungeonAccess && mmOracle.SetDungeonAccess) {
                const auto joint = jointMemo.Query(ootSeen, mmSeen);
                ootReach = joint.oot;
                mmReach = joint.mm;
            } else {
                auto ootQ = QueryReachableMemo(ootOracle, ootSeen, ootMemo);
                ootReach = ootQ.reach;
                const bool canEnterMm = ootQ.portalOpen && OwnsMmEntryItems(mmEntryItems, mmSeen);
                state.portalOpen = state.portalOpen || canEnterMm;
                mmReach = state.portalOpen ? QueryReachableMemo(mmOracle, mmSeen, mmMemo).reach : kEmptyReach;
            }
            if (ootReach->contains(MagicMirrorShuffle::kOotAccessProbe)) {
                auto forestReach = std::make_shared<std::unordered_set<std::string>>(*mmReach);
                MagicMirrorShuffle::AddOotRoute(*ootReach, *forestReach);
                mmReach = forestReach;
            }
            if (goalReached(*ootReach, *mmReach, state.ootOwned, state.mmOwned)) {
                return true;
            }
            bool changed = false;
            for (size_t i = 0; i < placements.size(); ++i) {
                if (state.credited[i] || i == excluded)
                    continue;
                const auto& p = placements[i];
                const auto& reach = (p.checkGame == GAME_OOT) ? *ootReach : *mmReach;
                if (reach.count(p.check)) {
                    (p.itemGame == GAME_OOT ? state.ootOwned : state.mmOwned).push_back(p.item);
                    state.credited[i] = true;
                    changed = true;
                    if (sphereOf)
                        (*sphereOf)[i] = sphere;
                }
            }
            if (!changed)
                return false;
        }
    };

    auto t0 = std::chrono::steady_clock::now();

    std::vector<size_t> candAll;
    for (size_t i = 0; i < placements.size(); ++i)
        if (placements[i].advancement)
            candAll.push_back(i);

    std::vector<signed char> classified(placements.size(), -1);

    ReplayState empty;
    empty.credited.assign(placements.size(), 0);
    empty.portalOpen = !portalGated || mmStart;
    std::vector<ReplayState> baseSpheres;
    std::vector<int> baseSphereOf(placements.size(), -1);
    ++result.replayedCount;
    bool baseWon = replay(empty, kExcludeNone, &baseSpheres, &baseSphereOf);

    std::vector<size_t> cand;
    if (baseWon) {
        for (size_t i : candAll)
            if (baseSphereOf[i] >= 0)
                cand.push_back(i);
            else
                classified[i] = 0;
    } else {
        for (size_t i : candAll)
            classified[i] = 1;
    }
    result.candidateCount = static_cast<int>(cand.size());
    if (progress) {
        progress->total.store(result.candidateCount);
        progress->placed.store(0);
    }

    std::map<std::tuple<int, int, std::string>, signed char> sameSphereCopies;
    for (size_t i : cand) {
        if (GenCancelled(progress))
            break;
        const auto& p = placements[i];
        const auto copy = std::make_tuple(baseSphereOf[i], static_cast<int>(p.itemGame), p.item);
        const auto known = sameSphereCopies.find(copy);
        if (known != sameSphereCopies.end()) {
            classified[i] = known->second;
        } else {
            ++result.replayedCount;
            classified[i] = replay(baseSpheres[baseSphereOf[i]], i, nullptr, nullptr) ? 0 : 1;
            sameSphereCopies.emplace(copy, classified[i]);
        }
        if (progress)
            progress->placed.fetch_add(1);
    }

    for (size_t pi : candAll) {
        const auto& p = placements[pi];
        bool required = classified[pi] == 1;
        result.requiredByCheck[checkKey(p)] = required;
        const auto& areaMap = (p.checkGame == GAME_OOT) ? ootCheckAreas : mmCheckAreas;
        auto ait = areaMap.find(p.check);
        if (ait != areaMap.end()) {
            std::string areaKey = (p.checkGame == GAME_OOT ? "oot:" : "mm:") + ait->second;
            if (required)
                result.areaHasRequired[areaKey] = true;
            else
                result.areaHasRequired.emplace(areaKey, false);
        }
    }
    result.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    if (mmRestore)
        mmRestore();

    std::cout << "[PLAYTHROUGH] pare-down: " << result.candidateCount << " advancement candidates, "
              << result.replayedCount << " oracle replays, " << result.ms << " ms\n";
    return result;
}

struct PlaythroughResult {
    bool beatable = false;
    int beatableSphere = -1;
    size_t reachableOot = 0, unreachableOot = 0;
    size_t reachableMm = 0, unreachableMm = 0;
    bool ganonReachable = false, majoraReachable = false;
    bool hunt = false;
    int piecesReachable = 0, piecesRequired = 0;
};

// progressionOnly must stay false for validation: the affordability check needs junk in priced shop slots.
inline PlaythroughResult RunPlaythrough(const std::string& spoilerJson, const OracleFns& ootOracle,
                                        const OracleFns& mmOracle, const std::string& seedLabel, void (*mmRestore)(),
                                        nlohmann::json* playthroughOut = nullptr, const std::string& sohDumpJson = "",
                                        const std::string& mmDumpJson = "", bool portalGated = true,
                                        bool progressionOnly = false, CwGoal goal = {}, bool mmStart = false,
                                        uint64_t sharedMask = 0, const std::string& sharedPairsJson = "") {
    static const char* kOotGanon = "Ganon";
    static const char* kMmWin = "Moon Majora Pot 01";

    PlaythroughResult result;
    result.hunt = goal.hunt;
    result.piecesRequired = goal.required;

    using Placed = CwPlacedItem;
    std::vector<Placed> placements = ParseSpoilerPlacements(spoilerJson, sohDumpJson, mmDumpJson);
    const auto sharedGroups = BuildSharedItemGroups(ResolveSharedPairs(sharedPairsJson, mmDumpJson, sohDumpJson),
                                                    SharedMaskForSpoiler(spoilerJson, sharedMask));

    auto queryReachable = QueryReachable;

    std::vector<std::string> ownedOot, ownedMm;
    std::unordered_set<std::string> collected;
    std::ostringstream log;
    log << "Cross-world playthrough - seed '" << seedLabel << "'\n";
    if (goal.hunt) {
        log << "Beatable when: " << goal.required << " Triforce Pieces collected across both games.\n\n";
    } else {
        log << "Beatable when: Ganon reachable (OOT: RC_GANON, i.e. bridge + boss key)"
            << (goal.either ? " OR" : " AND") << " Majora's Lair reachable (MM).\n\n";
    }

    int beatableSphere = -1;
    const int kMaxSpheres = 200;
    const bool isMmOpenFromStart = !portalGated || mmStart;
    bool portalOpen = isMmOpenFromStart;
    const std::vector<std::string> mmEntryItems = MmEntryItems(mmDumpJson);
    std::vector<std::string> ootSeen, mmSeen;
    for (int sphere = 0; sphere < kMaxSpheres; ++sphere) {
        SharedOwnedViews(sharedGroups, ownedOot, ownedMm, ootSeen, mmSeen);
        std::unordered_set<std::string> ootReach, mmReach;
        if (ootOracle.SetDungeonAccess && mmOracle.SetDungeonAccess) {
            const MmOverworld mmOverworld = ResolveMmOverworld(isMmOpenFromStart, mmEntryItems, mmSeen);
            auto joint = CrossDungeon::QueryJoint(ootOracle, mmOracle, ootSeen, mmSeen, mmOverworld, queryReachable);
            ootReach = std::move(joint.oot);
            mmReach = std::move(joint.mm);
        } else {
            ootReach = queryReachable(ootOracle, ootSeen);
            const bool canEnterMm = OraclePortalOpen(ootOracle) && OwnsMmEntryItems(mmEntryItems, mmSeen);
            portalOpen = portalOpen || canEnterMm;
            mmReach = portalOpen ? queryReachable(mmOracle, mmSeen) : std::unordered_set<std::string>{};
        }
        MagicMirrorShuffle::AddOotRoute(ootReach, mmReach);
        bool canGanon = ootReach.count(kOotGanon) > 0;
        bool canMajora = mmReach.count(kMmWin) > 0;
        if (goal.hunt ? CountOwnedTriforcePieces(ownedOot, ownedMm) >= goal.required
                      : IsBossGoalMet(goal, canGanon, canMajora)) {
            beatableSphere = sphere;
            break;
        }

        std::vector<Placed> newly;
        for (auto& p : placements) {
            std::string key = (p.checkGame == GAME_OOT ? "oot:" : "mm:") + p.check;
            if (collected.count(key))
                continue;
            const auto& reach = (p.checkGame == GAME_OOT) ? ootReach : mmReach;
            if (reach.count(p.check))
                newly.push_back(p);
        }
        if (newly.empty()) {
            log << "Sphere " << sphere << ": (stuck — nothing new reachable, not yet beatable)\n";
            break;
        }
        size_t newlyAdv = 0;
        for (auto& p : newly)
            newlyAdv += p.advancement ? 1 : 0;
        log << "Sphere " << sphere << "  (";
        if (goal.hunt) {
            log << "pieces=" << CountOwnedTriforcePieces(ownedOot, ownedMm) << "/" << goal.required;
        } else {
            log << "Ganon=" << (canGanon ? "Y" : "n") << " Majora=" << (canMajora ? "Y" : "n");
        }
        log << ", +" << newly.size() << " items, " << newlyAdv << " progression)\n";
        nlohmann::json sphereSteps = nlohmann::json::array();
        for (auto& p : newly) {
            std::string key = (p.checkGame == GAME_OOT ? "oot:" : "mm:") + p.check;
            collected.insert(key);
            (p.itemGame == GAME_OOT ? ownedOot : ownedMm).push_back(p.item);
            if (playthroughOut && (p.advancement || !progressionOnly)) {
                nlohmann::json step = { { "game", p.checkGame == GAME_OOT ? "oot" : "mm" },
                                        { "check", p.check },
                                        { "item", p.item },
                                        { "foreign", p.checkGame != p.itemGame } };
                if (!progressionOnly)
                    step["advancement"] = p.advancement;
                sphereSteps.push_back(std::move(step));
            }
            if (!p.advancement)
                continue;
            log << "    [" << (p.checkGame == GAME_OOT ? "OOT" : "MM ") << "] " << p.check << "  <-  " << p.item
                << (p.checkGame != p.itemGame ? (p.itemGame == GAME_OOT ? "  (OOT item)" : "  (MM item)") : "") << "\n";
        }
        if (playthroughOut)
            playthroughOut->push_back({ { "sphere", sphere }, { "steps", std::move(sphereSteps) } });
    }

    std::vector<std::string> allOot, allMm;
    for (auto& p : placements)
        (p.itemGame == GAME_OOT ? allOot : allMm).push_back(p.item);
    SharedOwnedViews(sharedGroups, allOot, allMm, ootSeen, mmSeen);
    std::unordered_set<std::string> everReachOot, everReachMm;
    if (ootOracle.SetDungeonAccess && mmOracle.SetDungeonAccess) {
        const MmOverworld mmOverworld = ResolveMmOverworld(isMmOpenFromStart, mmEntryItems, mmSeen);
        auto joint = CrossDungeon::QueryJoint(ootOracle, mmOracle, ootSeen, mmSeen, mmOverworld, queryReachable);
        everReachOot = std::move(joint.oot);
        everReachMm = std::move(joint.mm);
    } else {
        everReachOot = queryReachable(ootOracle, ootSeen);
        const bool canEnterMm = OraclePortalOpen(ootOracle) && OwnsMmEntryItems(mmEntryItems, mmSeen);
        const bool everPortalOpen = isMmOpenFromStart || canEnterMm;
        everReachMm = everPortalOpen ? queryReachable(mmOracle, mmSeen) : std::unordered_set<std::string>{};
    }
    MagicMirrorShuffle::AddOotRoute(everReachOot, everReachMm);
    result.ganonReachable = everReachOot.count(kOotGanon) > 0;
    result.majoraReachable = everReachMm.count(kMmWin) > 0;
    if (goal.hunt) {
        for (auto& p : placements) {
            if (CwIsTriforcePiece(p.itemGame, p.item) &&
                (p.checkGame == GAME_OOT ? everReachOot : everReachMm).count(p.check)) {
                ++result.piecesReachable;
            }
        }
        result.ganonReachable = result.majoraReachable = result.piecesReachable >= goal.required;
    }

    if (mmRestore)
        mmRestore();

    if (beatableSphere >= 0 && goal.hunt) {
        log << "\nBEATABLE at sphere " << beatableSphere << ": " << goal.required
            << " Triforce Pieces collected. Seed is completable.\n";
    } else if (beatableSphere >= 0) {
        log << "\nBEATABLE at sphere " << beatableSphere << ": "
            << (goal.either ? "Ganon OR Majora reachable" : "Ganon AND Majora both reachable")
            << ". Seed is completable.\n";
    } else {
        log << "\nNOT proven beatable within " << kMaxSpheres << " spheres (see stuck note above).\n";
    }

    auto emitGame = [&](GameId cg, const char* tag, const std::unordered_set<std::string>& everReach,
                        size_t& reachedOut, size_t& missingOut) {
        size_t reached = 0, missing = 0;
        log << "\n--- " << tag << " placements ---\n";
        for (auto& p : placements) {
            if (p.checkGame != cg)
                continue;
            bool got = everReach.count(p.check) > 0;
            got ? ++reached : ++missing;
            if (got && !p.advancement)
                continue;
            log << "    " << (got ? "  " : "! ") << p.check << "  <-  " << p.item
                << (p.checkGame != p.itemGame ? (p.itemGame == GAME_OOT ? "  (OOT item)" : "  (MM item)") : "")
                << (got ? "" : "   [UNREACHABLE]") << "\n";
        }
        log << "  " << tag << ": " << reached << " reachable, " << missing << " unreachable\n";
        reachedOut = reached;
        missingOut = missing;
    };
    log << "\n==== Placement (progression + unreachable; counts cover all checks) ====\n";
    emitGame(GAME_OOT, "OOT", everReachOot, result.reachableOot, result.unreachableOot);
    emitGame(GAME_MM, "MM", everReachMm, result.reachableMm, result.unreachableMm);

    if (!playthroughOut) {
        std::error_code ec;
        std::filesystem::create_directories("saves/combo", ec);
        std::ofstream f("saves/combo/slot0.playthrough.txt", std::ios::trunc);
        f << log.str();
        std::cout << "[PLAYTHROUGH] full sphere log -> saves/combo/slot0.playthrough.txt\n";
    }

    std::cout << "[PLAYTHROUGH] seed '" << seedLabel << "' - " << (beatableSphere >= 0 ? "BEATABLE" : "NOT beatable")
              << (beatableSphere >= 0 ? (" at sphere " + std::to_string(beatableSphere)) : "") << "\n";
    std::cout << "[PLAYTHROUGH] collected " << collected.size() << " items across "
              << (beatableSphere >= 0 ? beatableSphere : kMaxSpheres) << " spheres before the win\n";

    result.beatable = beatableSphere >= 0;
    result.beatableSphere = beatableSphere;
    return result;
}

inline nlohmann::json PlaythroughLines(const nlohmann::json& spheres) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& sphere : spheres) {
        nlohmann::json steps = nlohmann::json::array();
        for (const auto& step : sphere.value("steps", nlohmann::json::array())) {
            steps.push_back("[" + std::string(step.value("game", "") == "mm" ? "MM" : "OOT") + "] " +
                            step.value("check", "") + " --> " + step.value("item", ""));
        }
        out.push_back({ { "sphere", sphere.value("sphere", -1) }, { "steps", std::move(steps) } });
    }
    return out;
}

} // namespace ComboRando
