// Compiles the production header-only fill, display, playthrough and hint code.
#include "rando/ComboPlaythrough.h"
#ifndef COMBO_SHARED_BASELINE
#include "rando/CrossHints.h"
#endif
#include "rando/SharedItems.h"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace ComboRando;
using json = nlohmann::json;
static std::vector<std::string> owned[2];
static std::vector<std::string> checks[2];
static std::string required[2];
static int requiredCount[2] = { 0, 0 };
static bool openPortal = true;
static std::string reachable[2];
static int failures = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}
static void reset() {
}
static void setOot(const char* s) {
    owned[0] = json::parse(s).get<std::vector<std::string>>();
}
static void setMm(const char* s) {
    owned[1] = json::parse(s).get<std::vector<std::string>>();
}
static const char* reach(int g) {
    json out = checks[g];
    if (std::count(owned[g].begin(), owned[g].end(), required[g]) >= requiredCount[g])
        out.push_back(g == 0 ? "Ganon" : "Moon Majora Pot 01");
    reachable[g] = out.dump();
    return reachable[g].c_str();
}
static const char* reachOot() {
    return reach(0);
}
static const char* reachMm() {
    return reach(1);
}
static void place(const char*, const char*) {
}
static uint8_t portal() {
    return openPortal;
}
static const OracleFns oot{ reset, setOot, reachOot, place, portal };
static const OracleFns mm{ reset, setMm, reachMm, place };
static json emptyDump() {
    return { { "items", json::array() },
             { "pool", json::array() },
             { "checks", json::array() },
             { "accessibility", { { "maskQuestShuffle", true } } } };
}
static void add(json& d, std::string name, int copies, bool advancement = true) {
    d["items"].push_back({ { "name", name }, { "token", "RI_" + name } });
    for (int i = 0; i < copies; ++i) {
        d["pool"].push_back(
            { { "name", name }, { "advancement", advancement }, { "category", advancement ? "major" : "junk" } });
        d["checks"].push_back({ { "name", "Check " + std::to_string(d["checks"].size()) } });
    }
}
static void configure(const json& o, const json& m, std::string oReq = "", int oCount = 0, std::string mReq = "",
                      int mCount = 0) {
    for (int g = 0; g < 2; ++g) {
        owned[g].clear();
        checks[g].clear();
        for (const auto& c : (g == 0 ? o : m)["checks"])
            checks[g].push_back(c["name"]);
    }
    required[0] = oReq;
    requiredCount[0] = oCount;
    required[1] = mReq;
    requiredCount[1] = mCount;
    openPortal = true;
}
static std::string pairsJson(std::initializer_list<CwSharedPair> pairs) {
    json out = json::array();
    for (const auto& p : pairs)
        out.push_back({ { "oot", p.ootName }, { "mm", p.mmName } });
    return out.dump();
}
static void forwardAndRequiredness() {
    json o = emptyDump(), m = emptyDump();
    add(o, "Whip", 1);
    add(o, "Unrelated advancement", 1);
    add(m, "Junk", 1, false);
    configure(o, m, "", 0, "Whip", 1);
    json seed = { { "oot", { { "Check 0", "Whip" }, { "Check 1", "Unrelated advancement" } } },
                  { "mm", { { "Check 0", "Junk" } } },
                  { "foreign", json::array() } };
    auto pairs = pairsJson({ { "Whip", "Whip" } });
    auto run = RunPlaythrough(seed.dump(), oot, mm, "combo_shared", nullptr, nullptr, o.dump(), m.dump(), true, false,
                              {}, false
#ifndef COMBO_SHARED_BASELINE
                              ,
                              0, pairs
#endif
    );
    check(run.beatable, "NEI pair credits MM in forward playthrough");
    auto req = PareDownPlaythrough(seed.dump(), oot, mm, nullptr, o.dump(), m.dump(), {}, {}, DefaultGanonMajoraGoal,
                                   true, false, nullptr
#ifndef COMBO_SHARED_BASELINE
                                   ,
                                   0, pairs
#endif
    );
    check(req.requiredByCheck["oot:Check 0"], "NEI shared gate item remains required");
    check(!req.requiredByCheck["oot:Check 1"], "unrelated advancement is not required by a missing NEI mirror");
#ifndef COMBO_SHARED_BASELINE
    seed["oot"]["Check 0"] = "Fire Arrows";
    seed["sharedItems"] = { "fireArrows" };
    required[1] = "Fire Arrows";
    req = PareDownPlaythrough(seed.dump(), oot, mm, nullptr, o.dump(), m.dump());
    check(req.requiredByCheck["oot:Check 0"] && !req.requiredByCheck["oot:Check 1"],
          "requiredness consumes the seed's effective family mask");
    // Explicit requested mask must not override an effective mask which rejected that family.
    seed["sharedItems"] = json::array();
    run = RunPlaythrough(seed.dump(), oot, mm, "ineffective", nullptr, nullptr, o.dump(), m.dump(), true, false, {},
                         false, uint64_t{ 1 } << SF_FIRE_ARROWS);
    check(!run.beatable, "seed effective mask takes precedence over the requested mask");
    openPortal = false;
    run = RunPlaythrough(seed.dump(), oot, mm, "no-logic", nullptr, nullptr, o.dump(), m.dump(), false, false, {},
                         false, 0, pairs);
    check(run.reachableMm == 1, "ungated ever-reachable inventory honors No Logic");
    openPortal = true;
#endif
}
static void parsedNamesAndDisplay() {
    json o = emptyDump(), m = emptyDump();
    add(o, "Goron Mask (MM)", 1);
    add(o, "Goron Mask", 1);
    add(m, "Goron Mask", 1);
    json seed = { { "oot", { { "native", "Goron Mask (MM)" }, { "tagged", "Goron Mask (OOT)" } } },
                  { "mm", { { "foreign", "Goron Mask (MM)" } } },
                  { "foreign", json::array({ { { "checkGame", "mm" },
                                               { "checkName", "foreign" },
                                               { "itemGame", "oot" },
                                               { "itemName", "Goron Mask (MM)" } } }) } };
    auto pl = ParseSpoilerPlacements(seed.dump(), o.dump(), m.dump());
    for (const auto& p : pl)
        check(p.item == (p.check == "tagged" ? "Goron Mask" : "Goron Mask (MM)"),
              "preserve intrinsic NEI mask name for " + p.check);
    auto foreign = json::array({ { { "checkGame", "mm" },
                                   { "checkName", "whip" },
                                   { "itemGame", "oot" },
                                   { "itemName", "Whip" },
                                   { "displayName", "Whip" } },
                                 { { "checkGame", "oot" },
                                   { "checkName", "junk" },
                                   { "itemGame", "mm" },
                                   { "itemName", "Arrows" },
                                   { "displayName", "Arrows" } } });
    auto enriched = BuildForeignArray(foreign
#ifndef COMBO_SHARED_BASELINE
                                      ,
                                      0, { { "Whip", "Whip" } }
#endif
    );
    check(enriched[0]["displayName"] == "Whip" && enriched[0].value("shared", false),
          "NEI foreign pickup is untagged and shared");
    check(enriched[1]["displayName"] == "Arrows (MM)" && !enriched[1].value("shared", false),
          "unshared foreign junk keeps origin");
#ifndef COMBO_SHARED_BASELINE
    auto names = SharedUntaggedNames(uint64_t{ 1 } << SF_GORON_MASK, { { "Goron Mask (MM)", "Goron Mask" } });
    json op = { { "alias", "Goron Mask (MM)" }, { "native", "Goron Mask" } }, mp = { { "native", "Goron Mask" } };
    SuffixCrossGameItems(op, mp, json::array(), o.dump(), m.dump(), names);
    check(op["alias"] == "Goron Mask (MM)" && op["native"] == "Goron Mask" && mp["native"] == "Goron Mask",
          "both shared naming systems exempt native suffixes");
    ForeignItem fi;
    fi.shared = true;
    fi.itemGame = GAME_OOT;
    check(ShownForeignName(fi, "Whip") == "Whip", "live resolved shared names remain untagged");
#endif
}
#ifndef COMBO_SHARED_BASELINE
static int countPlaced(const CombinedFillResult& r, GameId game, const std::string& name) {
    int count = 0;
    for (const auto& p : ParseSpoilerPlacements(r.spoilerJson))
        if (p.itemGame == game && p.item == name)
            ++count;
    return count;
}
static CombinedFillResult fill(json o, json m, uint64_t mask, const std::string& pairs,
                               const std::string& forced = "") {
    configure(o, m);
    return CrossWorldCombinedFill(o.dump(), m.dump(), 42, oot, mm, nullptr, forced, OotAccess::ALL_REACHABLE, {},
                                  GAME_OOT, pairs, mask);
}
static void fillAndCredit() {
    auto pairs = pairsJson({ { "Progressive Bow", "Progressive Bow" }, { "Progressive Bow", "Progressive Bow" } });
    json o = emptyDump(), m = emptyDump();
    add(o, "Progressive Bow", 3);
    add(o, "Junk", 1, false);
    add(m, "Progressive Bow", 4);
    add(m, "Junk", 1, false);
    auto r = fill(o, m, uint64_t{ 1 } << SF_BOW, pairs);
    check(r.success, "overlapping family/NEI fill succeeds");
    if (r.success) {
        check(countPlaced(r, GAME_OOT, "Progressive Bow") == 3 && countPlaced(r, GAME_MM, "Progressive Bow") == 1,
              "overlap retains NEI's larger tier count exactly once");
        check(SharedMaskFromKeys(json::parse(r.spoilerJson)["sharedItems"]) == (uint64_t{ 1 } << SF_BOW),
              "overlap still records the effective family");
    }
    o = emptyDump();
    m = emptyDump();
    add(o, "Goron Mask", 1);
    add(o, "Goron Mask (MM)", 1);
    add(o, "Junk", 1, false);
    add(m, "Goron Mask", 1);
    add(m, "Junk", 1, false);
    pairs = pairsJson({ { "Goron Mask (MM)", "Goron Mask" } });
    r = fill(o, m, uint64_t{ 1 } << SF_GORON_MASK, pairs);
    check(r.success, "native/imported mask fill succeeds");
    if (r.success)
        check(countPlaced(r, GAME_OOT, "Goron Mask") == 1 && countPlaced(r, GAME_OOT, "Goron Mask (MM)") == 0 &&
                  countPlaced(r, GAME_MM, "Goron Mask") == 0,
              "three shared mask representations retain one physical copy");
    o["accessibility"]["maskQuestShuffle"] = false;
    r = fill(o, m, uint64_t{ 1 } << SF_GORON_MASK, pairs);
    check(r.success, "mask shuffle disabled fill succeeds");
    if (r.success)
        check(json::parse(r.spoilerJson)["sharedItems"].empty() && countPlaced(r, GAME_OOT, "Goron Mask") == 1 &&
                  countPlaced(r, GAME_OOT, "Goron Mask (MM)") == 1,
              "disabled mask family preserves distinct native and NEI masks");
    o = emptyDump();
    m = emptyDump();
    add(o, "Fire Arrows", 1);
    add(o, "Junk", 1, false);
    add(m, "Fire Arrows", 1);
    add(m, "Junk", 1, false);
    r = fill(o, m, uint64_t{ 1 } << SF_FIRE_ARROWS, "", R"({"Link's Pocket":{"item":"Fire Arrows"}})");
    check(r.success &&
              SharedMaskFromKeys(json::parse(r.spoilerJson)["sharedItems"]) == (uint64_t{ 1 } << SF_FIRE_ARROWS),
          "forced starting copy activates the family before pool reservation");
    o = emptyDump();
    m = emptyDump();
    add(o, "Junk", 1, false);
    add(m, "Fire Arrows", 1);
    add(m, "Junk", 1, false);
    r = fill(o, m, uint64_t{ 1 } << SF_FIRE_ARROWS, "");
    check(r.success && json::parse(r.spoilerJson)["sharedItems"].empty() && countPlaced(r, GAME_MM, "Fire Arrows") == 1,
          "ineffective family preserves MM pool");
    o = emptyDump();
    m = emptyDump();
    add(o, "Junk", 1, false);
    add(m, "Whip", 1);
    add(m, "Junk", 1, false);
    o["fixed"] = json::array({ { { "check", "Fixed Whip" }, { "item", "Whip" }, { "advancement", true } } });
    r = fill(o, m, 0, pairsJson({ { "Whip", "Whip" } }));
    check(r.success && countPlaced(r, GAME_MM, "Whip") == 0,
          "a shared mod item fixed in OoT removes MM's shuffled copy");
    auto groups = BuildSharedItemGroups({ { "Progressive Bow", "Progressive Bow" } }, uint64_t{ 1 } << SF_BOW);
    std::vector<std::string> ov, mv;
    SharedOwnedViews(groups, { "Progressive Bow" }, {}, ov, mv);
    check(ov.size() == 1 && mv.size() == 1, "one shared bow never credits tier two");
    SharedOwnedViews(groups, { "Progressive Bow", "Progressive Bow" }, { "Progressive Bow" }, ov, mv);
    check(ov.size() == 3 && mv.size() == 3, "independent shared progressive pickups accumulate once each");
    groups = BuildSharedItemGroups({ { "Goron Mask (MM)", "Goron Mask" } }, uint64_t{ 1 } << SF_GORON_MASK);
    SharedOwnedViews(groups, { "Goron Mask" }, {}, ov, mv);
    check(std::count(ov.begin(), ov.end(), "Goron Mask (MM)") == 1 && mv.size() == 1,
          "native mask credits imported alias via shared component");
    groups = BuildSharedItemGroups({}, uint64_t{ 1 } << SF_HOOKSHOT);
    SharedOwnedViews(groups, { "Progressive Hookshot", "Progressive Hookshot" }, {}, ov, mv);
    check(mv.size() == 1, "family-only MM hookshot mirror honors its tier cap");
    o = emptyDump();
    m = emptyDump();
    add(o, "Kokiri Sword", 1);
    add(o, "Junk", 1, false);
    add(m, "Progressive Sword", 3);
    add(m, "Junk", 1, false);
    r = fill(o, m, uint64_t{ 1 } << SF_SWORD, "");
    check(r.success && countPlaced(r, GAME_OOT, "Kokiri Sword") == 1 &&
              countPlaced(r, GAME_MM, "Progressive Sword") == 2,
          "shared swords keep MM's tiers when OoT has no upgrade chain");
    add(o, "Kokiri Sword", 2);
    r = fill(o, m, uint64_t{ 1 } << SF_SWORD, "");
    check(r.success && countPlaced(r, GAME_OOT, "Kokiri Sword") == 3 &&
              countPlaced(r, GAME_MM, "Progressive Sword") == 0,
          "OoT's sword chain supplies every shared sword tier");
    groups = BuildSharedItemGroups({}, uint64_t{ 1 } << SF_SWORD);
    SharedOwnedViews(groups, { "Kokiri Sword" }, { "Progressive Sword" }, ov, mv);
    check(ov.size() == 2 && mv.size() == 2, "a sword tier found in either game reaches both");
    o = emptyDump();
    m = emptyDump();
    add(o, "Strength Upgrade", 4);
    add(o, "Junk", 1, false);
    add(m, "oot.power_bracelet", 1);
    add(m, "oot.progressive.strength", 3);
    add(m, "Junk", 1, false);
    const uint64_t strength = SharedMaskFromKeys(json::array({ "grab" }));
    r = fill(o, m, strength, "");
    check(r.success && countPlaced(r, GAME_OOT, "Strength Upgrade") == 4 &&
              countPlaced(r, GAME_MM, "oot.power_bracelet") == 0 &&
              countPlaced(r, GAME_MM, "oot.progressive.strength") == 0,
          "both MM strength pickups are one chain with OoT's Strength Upgrade");
    groups = BuildSharedItemGroups({}, strength);
    SharedOwnedViews(groups, { "Strength Upgrade", "Strength Upgrade" }, {}, ov, mv);
    check(groups.size() == 1 && std::count(mv.begin(), mv.end(), "oot.progressive.strength") == 2,
          "every strength level found in OoT reaches MM");
    groups = BuildSharedItemGroups({}, uint64_t{ 1 } << SF_WALLET);
    SharedOwnedViews(groups, { "Progressive Wallet" }, {}, ov, mv);
    check(std::count(mv.begin(), mv.end(), "nei.bag.child_wallet") == 1 &&
              std::count(mv.begin(), mv.end(), "Progressive Wallet") == 0,
          "OoT's first wallet is MM's Child Wallet");
    SharedOwnedViews(groups, std::vector<std::string>(5, "Progressive Wallet"), {}, ov, mv);
    check(std::count(mv.begin(), mv.end(), "Progressive Wallet") == 3 &&
              std::count(mv.begin(), mv.end(), "nei.infinite.wallet") == 1,
          "OoT's fifth wallet is MM's Infinite Wallet");
    auto unresolved = ResolveSharedPairs(R"([{"oot":"Unknown","mm":"RI_MISSING"}])", emptyDump().dump());
    check(unresolved.empty(), "unresolved RI token cannot become a false shared edge");
}
static void ganondorfHint() {
    json o = emptyDump(), m = emptyDump();
    add(o, "Progressive Master Sword", 1);
    add(o, "Light Arrows", 1);
    add(m, "Junk", 1, false);
    json seed = { { "oot", { { "Check 0", "Progressive Master Sword" }, { "Check 1", "Light Arrows" } } },
                  { "mm", json::object() },
                  { "foreign", json::array() } };
    json hints = { { "options", { { "ganondorfHint", 1 }, { "shuffleMasterSword", 1 } } },
                   { "checks", json::array({ { { "name", "Check 0" }, { "area", "Sword Area" } },
                                             { { "name", "Check 1" }, { "area", "Arrow Area" } } }) },
                   { "hintTextTable", json::object() } };
    for (const char* key :
         { "RHT_GANONDORF_HINT_LA_ONLY", "RHT_GANONDORF_HINT_MS_ONLY", "RHT_GANONDORF_HINT_LA_AND_MS" })
        hints["hintTextTable"][key] = {
            { "clear", { { "en", "arrows [[1]] sword [[2]]" }, { "de", "[[1]] [[2]]" }, { "fr", "[[1]] [[2]]" } } }
        };
    auto generated = Generate(42, o.dump(), hints.dump(), m.dump(), json::array(), seed.dump(), {});
    bool found = false;
    for (const auto& hint : generated["oot"]) {
        if (hint.value("checkName", "") != kGanondorfHintKey)
            continue;
        found = true;
        check(hint["messages"].size() == 3, "progressive Master Sword retains all Ganondorf message indices");
        check(hint["messages"][1]["en"].get<std::string>().find("Sword Area") != std::string::npos,
              "Ganondorf sword clause resolves the progressive chain location");
    }
    check(found, "Ganondorf hint survives NEI progressive Master Sword");
}
#endif
static void dynamicModPairs() {
    auto o = emptyDump();
    auto m = emptyDump();
    add(o, "oot rod", 3);
    add(m, "mm rod", 3);
    add(o, "Rupees (5)", 4, false);
    add(m, "Green Rupee", 4, false);
    o["items"][0]["textKey"] = "nei.somaria";
    m["items"][0]["textKey"] = "nei.somaria";
    auto pairs = ResolveModPairs(o.dump(), m.dump());
    check(pairs.size() == 1 && pairs[0].key == "nei.somaria", "pair by key despite different display names");
    check(EncodeModPairs(pairs)[0]["mm"] == "mm rod", "dynamic pair survives serialization");
    configure(o, m, "oot rod", 3, "mm rod", 3);
    auto result = CrossWorldCombinedFill(o.dump(), m.dump(), 1, oot, mm, nullptr, "", OotAccess::ALL_REACHABLE);
    check(result.success, "dynamic progressive family satisfies both logics");
    if (result.success) {
        check(json::parse(result.spoilerJson)["modFamilies"].size() == 1, "seed stores dynamic family");
    }
    for (bool plentiful : { false, true }) {
        auto preview = CrossWorldCombinedFill(o.dump(), m.dump(), 1, oot, mm, nullptr, "", OotAccess::ALL_REACHABLE, {},
                                              GAME_OOT, "", 0, true, plentiful);
        check(preview.success, "dynamic pool preview succeeds");
        int copies = 0;
        for (const auto& entry : preview.pool) {
            if (entry["name"] == "oot rod" || entry["name"] == "mm rod") {
                copies += entry["shuffled"].get<int>();
                check(entry["shared"] == true, "paired preview entry is marked shared");
            }
        }
        check(copies == (plentiful ? 6 : 3), "plentiful preserves both progressive pools; default keeps one");
    }
    const std::string unshared = EncodeUnsharedSpec({ "nei.somaria" });
    check(ResolveSharedPairs(unshared, m.dump(), o.dump()).empty(), "an unshared mod pair is not resolved");
    check(EncodeUnsharedSpec({}).empty() &&
              ResolveSharedPairs(EncodeUnsharedSpec({ "nei.other" }), m.dump(), o.dump()).size() == 1,
          "unsharing another key leaves the pair shared");
    auto separate = CrossWorldCombinedFill(o.dump(), m.dump(), 1, oot, mm, nullptr, "", OotAccess::ALL_REACHABLE, {},
                                           GAME_OOT, unshared, 0, true);
    int separateCopies = 0;
    for (const auto& entry : separate.pool) {
        if (entry["name"] == "oot rod" || entry["name"] == "mm rod") {
            separateCopies += entry["shuffled"].get<int>();
            check(!entry.value("shared", false), "an unshared pair is not marked shared");
        }
    }
    check(separate.success && separateCopies == 6, "an unshared pair keeps both games' copies");
    auto separateSeed = CrossWorldCombinedFill(o.dump(), m.dump(), 1, oot, mm, nullptr, "", OotAccess::ALL_REACHABLE,
                                               {}, GAME_OOT, unshared);
    check(separateSeed.success && json::parse(separateSeed.spoilerJson)["modFamilies"].empty(),
          "the seed of an unshared pair stores no family for it");
    auto groups = BuildSharedItemGroups(pairs, 0);
    std::vector<std::string> a, b;
    SharedOwnedViews(groups, { "oot rod", "oot rod", "oot rod" }, {}, a, b);
    check(std::count(b.begin(), b.end(), "mm rod") == 3, "dynamic family preserves progressive levels");
    m["items"][0]["textKey"] = "nei.other";
    check(ResolveModPairs(o.dump(), m.dump()).empty(), "one-game mod stays unpaired");
    m["items"][0]["textKey"] = "nei.combo.letter";
    o["items"][0]["textKey"] = "nei.combo.letter";
    check(ResolveModPairs(o.dump(), m.dump()).empty(), "presentation-only compatibility items stay unpaired");
    check(sizeof(SharedMaskAll()) == 8, "shared masks use 64 bits");
    const auto wide = SharedMaskFromKeys(json::array({ "greatSpin", "scale", "fishingPole" }));
    check((wide & (uint64_t{ 1 } << SF_GREAT_SPIN)) != 0, "family above bit 31 survives key parsing");
    check(SharedMaskFromKeys(SharedKeysFromMask(wide)) == wide, "wide family survives seed serialization");
    check((SharedMaskAll() & wide) == wide, "all includes high families");
}
static void addKeyed(json& dump, const std::string& key) {
    add(dump, key, 1);
    dump["items"].back()["textKey"] = key;
}
static bool hasPair(const std::vector<CwSharedPair>& pairs, const std::string& key, const std::string& ootName,
                    const std::string& mmName) {
    return std::any_of(pairs.begin(), pairs.end(), [&](const CwSharedPair& pair) {
        return pair.key == key && pair.ootName == ootName && pair.mmName == mmName;
    });
}
static void nativePairKeys() {
    auto o = emptyDump();
    auto m = emptyDump();
    for (const char* native : { "Forest Medallion", "Kokiri's Emerald", "Triforce Piece", "Infinite Wallet" }) {
        add(o, native, 1);
    }
    for (const char* key : { "nei.mask_bunny", "nei.combo.bombers_notebook", "nei.combo.letter" }) {
        addKeyed(o, key);
    }
    for (const char* key :
         { "nei.medallion.forest", "nei.spiritual_stone.kokiri", "nei.infinite.wallet", "nei.combo.letter" }) {
        addKeyed(m, key);
    }
    for (const char* native : { "Piece of the Triforce", "Bunny Hood", "Bomber's Notebook" }) {
        add(m, native, 1);
    }
    const auto pairs = ResolveModPairs(o.dump(), m.dump());
    check(pairs.size() == 6, "every unkeyed native meets its counterpart, and the letter holder stays alone");
    check(hasPair(pairs, "nei.medallion.forest", "Forest Medallion", "nei.medallion.forest") &&
              hasPair(pairs, "nei.spiritual_stone.kokiri", "Kokiri's Emerald", "nei.spiritual_stone.kokiri") &&
              hasPair(pairs, "nei.infinite.wallet", "Infinite Wallet", "nei.infinite.wallet"),
          "an OoT collectable pairs with MM's mod copy");
    check(hasPair(pairs, "nei.mask_bunny", "nei.mask_bunny", "Bunny Hood") &&
              hasPair(pairs, "nei.combo.bombers_notebook", "nei.combo.bombers_notebook", "Bomber's Notebook"),
          "an MM native pairs with OoT's mod copy");
    check(hasPair(pairs, kTriforcePairKey, "Triforce Piece", "Piece of the Triforce"), "Triforce pieces pair");
    const std::string hunt = EncodeUnsharedSpec({ kTriforcePairKey });
    check(ResolveSharedPairs(hunt, m.dump(), o.dump()).size() == 5, "the hunt can keep its pieces apart");
    const auto groups =
        BuildSharedItemGroups({ { "nei.mask_bunny", "Bunny Hood", "nei.mask_bunny" } }, uint64_t{ 1 } << SF_BUNNY_HOOD);
    std::vector<std::string> ootView;
    std::vector<std::string> mmView;
    SharedOwnedViews(groups, {}, { "Bunny Hood" }, ootView, mmView);
    check(groups.size() == 1 && std::count(ootView.begin(), ootView.end(), "nei.mask_bunny") == 1 &&
              std::count(ootView.begin(), ootView.end(), "Bunny Hood") == 1,
          "a trade mask and its wearable are one shared item");
    const FoldedPair* folded = FindFoldedPair("nei.mask_bunny");
    check(folded != nullptr && folded->hostFamily == SF_BUNNY_HOOD && FindFoldedPair("nei.somaria") == nullptr,
          "a wearable follows its trade mask's option");
}
static void triforceTriggerPieces() {
    auto o = emptyDump();
    auto m = emptyDump();
    add(o, kOotTriforcePiece, 3);
    add(m, kMmTriforcePiece, 2);
    add(o, "Rupees (5)", 2, false);
    add(m, "Green Rupee", 2, false);
    configure(o, m, kOotTriforcePiece, 5, kMmTriforcePiece, 5);
    const auto result = CrossWorldCombinedFill(o.dump(), m.dump(), 1, oot, mm, nullptr, "", OotAccess::ALL_REACHABLE);
    check(result.success, "each game's triggers count the pieces of both pools");
    const auto preview = CrossWorldCombinedFill(o.dump(), m.dump(), 1, oot, mm, nullptr, "", OotAccess::ALL_REACHABLE,
                                                {}, GAME_OOT, "", 0, true);
    int copies = 0;
    for (const auto& entry : preview.pool) {
        if (entry["name"] == kOotTriforcePiece || entry["name"] == kMmTriforcePiece) {
            copies += entry["shuffled"].get<int>();
        }
    }
    check(preview.success && copies == 5, "paired trigger pieces keep both pools' copies");
    check(CwPoolPieces({ .hunt = false, .total = 9, .pieces = 4 }) == 4 &&
              CwPoolPieces({ .hunt = true, .required = 3, .total = 9, .pieces = 4 }) == 9,
          "the goal names its pool pieces");
}
static void addCopies(std::vector<CwItem>& pool, GameId game, const std::string& name, int copies) {
    for (int i = 0; i < copies; ++i) {
        pool.push_back({ game, name, true, CwCat::MAJOR });
    }
}
static size_t countCopies(const std::vector<CwItem>& pool, GameId game, const std::string& name) {
    return std::count_if(pool.begin(), pool.end(),
                         [&](const CwItem& item) { return item.game == game && item.name == name; });
}
static void bottleCap() {
    const auto groups = BuildSharedItemGroups({ { "Empty Bottle", "Empty Bottle", "fleet.bottle.empty" } }, 0);
    std::vector<CwItem> adv;
    std::vector<CwItem> junk = { { GAME_MM, "nei.bottle_7", false, CwCat::LESSER } };
    for (const char* name : { "Bottle with Big Poe", "Bottle with Blue Fire", "Bottle with Bugs", "Bottle with Fairy",
                              "Bottle with Green Potion", "Empty Bottle", "Buy Bottle Bug", "nei.bottomless_bottle" }) {
        addCopies(adv, GAME_OOT, name, 1);
    }
    addCopies(adv, GAME_OOT, "Bottle with Fish", 2);
    addCopies(adv, GAME_MM, "Bottle With Gold Dust", 2);
    addCopies(adv, GAME_MM, "Bottle of Chateau Romani", 2);
    addCopies(adv, GAME_MM, "Empty Bottle", 3);
    addCopies(adv, GAME_MM, "Chateau Romani Refill", 3);
    const CwBottleSlots extended{ { kExtendedBottleSlots, kExtendedBottleSlots } };
    check(CapBottles(CollectBottleKinds(adv, junk, {}, groups), extended, adv, junk, false) == 3,
          "bottle cap drops only what a game cannot hold");
    check(countCopies(adv, GAME_MM, "Empty Bottle") == 1, "shared bottle copies go first");
    check(countCopies(adv, GAME_OOT, "Bottle with Fish") == 1, "a duplicate goes before a shared last copy");
    check(countCopies(adv, GAME_OOT, "Empty Bottle") == 1 && countCopies(adv, GAME_MM, "Bottle With Gold Dust") == 2,
          "bottles a game has room for stay");
    check(junk.size() == 1 && countCopies(adv, GAME_MM, "Chateau Romani Refill") == 3 &&
              countCopies(adv, GAME_OOT, "Buy Bottle Bug") == 1 &&
              countCopies(adv, GAME_OOT, "nei.bottomless_bottle") == 1,
          "refills and the bottomless bottle are not bottles");
    std::vector<CwItem> distinct;
    for (int i = 0; i < 10; ++i) {
        addCopies(distinct, GAME_OOT, "Bottle with Thing " + std::to_string(i), 1);
    }
    std::vector<CwItem> none;
    check(CapBottles(CollectBottleKinds(distinct, none, {}, {}), extended, distinct, none, false) == 0 &&
              distinct.size() == 10,
          "the last progression copy is never dropped");
    std::vector<CwItem> vanilla;
    addCopies(vanilla, GAME_OOT, "Bottle with Fish", 6);
    addCopies(vanilla, GAME_MM, "Bottle of Milk", 8);
    check(CapBottles(CollectBottleKinds(vanilla, none, {}, {}), CwBottleSlots{}, vanilla, none, false) == 4 &&
              countCopies(vanilla, GAME_OOT, "Bottle with Fish") == 4 &&
              countCopies(vanilla, GAME_MM, "Bottle of Milk") == 6,
          "without the bottle mod OoT holds 4 bottles and MM 6");
    auto withMod = emptyDump();
    add(withMod, "nei.bottle_5", 0);
    const auto slots = ReadBottleSlots(withMod.dump(), emptyDump().dump());
    check(slots.perGame[GAME_OOT] == kExtendedBottleSlots && slots.perGame[GAME_MM] == 6,
          "the bottle mod widens only the game that lists its bottles");
}

#ifndef COMBO_SHARED_BASELINE
static CombinedFillResult fillWithRules(const json& o, const json& m, uint32_t seed, const CwFillRules& rules) {
    configure(o, m);
    return CrossWorldCombinedFill(o.dump(), m.dump(), seed, oot, mm, nullptr, "", OotAccess::ALL_REACHABLE, {},
                                  GAME_OOT, "", 0, false, false, rules);
}
static bool isPlacedOnlyIn(const CombinedFillResult& r, const std::string& item,
                           const std::function<bool(const CwPlacedItem&)>& allowed) {
    bool found = false;
    for (const auto& p : ParseSpoilerPlacements(r.spoilerJson)) {
        if (p.item != item)
            continue;
        found = true;
        if (!allowed(p))
            return false;
    }
    return found;
}
static void goalModes() {
    check(IsBossGoalMet({ .either = true }, true, false) && !IsBossGoalMet({}, true, false),
          "either needs one boss, both needs both");
    json o = emptyDump(), m = emptyDump();
    add(o, "Whip", 1);
    add(m, "Junk", 1, false);
    configure(o, m, "Whip", 1, "Never Placed", 1);
    const json seed = { { "oot", { { "Check 0", "Whip" } } },
                        { "mm", { { "Check 0", "Junk" } } },
                        { "foreign", json::array() } };
    json spheres = json::array();
    auto either = RunPlaythrough(seed.dump(), oot, mm, "either", nullptr, &spheres, o.dump(), m.dump(), true, false,
                                 { .either = true });
    check(either.beatable, "either goal is met by Ganon alone");
    spheres = json::array();
    auto both = RunPlaythrough(seed.dump(), oot, mm, "both", nullptr, &spheres, o.dump(), m.dump(), true, false, {});
    check(!both.beatable, "both goal still needs Majora");
    check(EitherGanonMajoraGoal({ "Ganon" }, {}, {}, {}) && !DefaultGanonMajoraGoal({ "Ganon" }, {}, {}, {}),
          "either requiredness goal accepts one boss");
}
static void plando() {
    const json fleet = { { "plando",
                           { { { "game", "mm" },
                               { "check", "Check 0" },
                               { "state", "item" },
                               { "itemGame", "oot" },
                               { "item", "Whip" } },
                             { { "game", "oot" }, { "check", "Check 1" }, { "state", "junk" } },
                             { { "game", "oot" }, { "check", "Check 2" }, { "state", "vanilla" } },
                             { { "game", "oot" }, { "state", "bogus" } } } },
                         { "poolEdits", { { { "game", "oot" }, { "item", "Junk" }, { "delta", -1 } } } } };
    const CwFillRules rules = ParseCwFillRules(fleet);
    check(rules.plando.size() == 3 && rules.poolEdits.size() == 1, "malformed plando rows are skipped");
    json o = emptyDump(), m = emptyDump();
    add(o, "Whip", 1);
    add(o, "Hammer", 1);
    add(o, "Junk", 3, false);
    o["checks"][2]["vanilla"] = "Hammer";
    add(m, "Junk", 2, false);
    auto r = fillWithRules(o, m, 7, rules);
    check(r.success, "plando fill succeeds: " + r.error);
    if (!r.success)
        return;
    const json spoiler = json::parse(r.spoilerJson);
    check(spoiler["mm"].value("Check 0", "") == "Whip" && countPlaced(r, GAME_OOT, "Whip") == 1,
          "a cross-game plando item sits at its check, once");
    check(spoiler["oot"].value("Check 1", "") == "Junk", "a junk plando row holds that game's junk");
    check(spoiler["oot"].value("Check 2", "") == "Hammer" && countPlaced(r, GAME_OOT, "Hammer") == 1,
          "a vanilla plando row keeps the check's vanilla item");
}
static void ownGameAndSongs() {
    check(CwOwnGameCategory({ GAME_MM, "Woodfall Map", false, CwCat::LESSER, "keys" }) == "mapCompass" &&
              CwOwnGameCategory({ GAME_OOT, "Forest Medallion", true, CwCat::MAJOR, "progression" }) ==
                  "dungeonReward" &&
              CwOwnGameCategory({ GAME_MM, kMmTriforcePiece, true, CwCat::MAJOR, "progression" }) == "triforce",
          "own-game categories come from category, group and name");
    json o = emptyDump(), m = emptyDump();
    add(o, "Small Key", 2);
    o["pool"][0]["category"] = "smallKey";
    o["pool"][1]["category"] = "smallKey";
    add(o, "Junk", 2, false);
    add(m, "Junk", 6, false);
    CwFillRules keys;
    keys.ownGame = { "smallKey" };
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        auto r = fillWithRules(o, m, seed, keys);
        check(r.success &&
                  isPlacedOnlyIn(r, "Small Key", [](const CwPlacedItem& p) { return p.checkGame == GAME_OOT; }),
              "own-game keys stay in their game (seed " + std::to_string(seed) + ")");
    }
    o = emptyDump();
    m = emptyDump();
    add(o, "Zelda's Lullaby", 1);
    o["items"].back()["song"] = true;
    add(o, "Junk", 3, false);
    add(m, "Sonata of Awakening", 1);
    m["items"].back()["song"] = true;
    add(m, "Junk", 3, false);
    m["checks"][2]["type"] = "song";
    o["checks"][3]["type"] = "song";
    CwFillRules songs;
    songs.songs = CwSongPlacement::SONG_LOCATIONS;
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        auto r = fillWithRules(o, m, seed, songs);
        const auto atSongCheck = [](const CwPlacedItem& p) {
            return (p.checkGame == GAME_MM && p.check == "Check 2") ||
                   (p.checkGame == GAME_OOT && p.check == "Check 3");
        };
        check(r.success && isPlacedOnlyIn(r, "Zelda's Lullaby", atSongCheck) &&
                  isPlacedOnlyIn(r, "Sonata of Awakening", atSongCheck),
              "songs land on song checks of either game (seed " + std::to_string(seed) + ")");
    }
    m["checks"][2]["type"] = "";
    auto overflow = fillWithRules(o, m, 3, songs);
    check(overflow.success, "a song past its confined checks still goes somewhere");
}
#endif

int main() {
    bottleCap();
    dynamicModPairs();
    nativePairKeys();
    triforceTriggerPieces();
    forwardAndRequiredness();
    parsedNamesAndDisplay();
#ifndef COMBO_SHARED_BASELINE
    fillAndCredit();
    ganondorfHint();
    goalModes();
    plando();
    ownGameAndSongs();
#endif
    if (failures) {
        std::cerr << failures << " combo shared checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "combo shared production behavior checks passed\n";
}
