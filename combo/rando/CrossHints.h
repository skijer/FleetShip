#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>

#include "ComboPlaythrough.h"
#include "CrossWorldRando.h"

namespace ComboRando {

inline bool NeedsRequirednessPareDown(const std::string& sohHintDumpJson, const std::string& mmDumpJson) {
    try {
        auto hd = nlohmann::json::parse(sohHintDumpJson.empty() ? "{}" : sohHintDumpJson);
        auto opts = hd.value("options", nlohmann::json::object());
        int gossipStoneHints = opts.value("gossipStoneHints", 0);
        int hintDistribution = opts.value("hintDistribution", 1);
        if (gossipStoneHints != 0 && hintDistribution != 0)
            return true;
    } catch (...) {}
    try {
        auto md = nlohmann::json::parse(mmDumpJson.empty() ? "{}" : mmDumpJson);
        auto opts = md.value("options", nlohmann::json::object());
        if (opts.value("RO_HINTS_GOSSIP_STONES", 0) != 0 || opts.value("RO_HINTS_PURCHASEABLE", 0) != 0)
            return true;
    } catch (...) {}
    return false;
}

// MM-sourced content has no translation, so its English text is duplicated into de/fr.
struct Tri {
    std::string en, de, fr;
};
inline Tri EnglishOnly(const std::string& s) {
    return { s, s, s };
}
inline Tri FromJson(const nlohmann::json& j) {
    return { j.value("en", ""), j.value("de", ""), j.value("fr", "") };
}
inline Tri& operator+=(Tri& a, const Tri& b) {
    a.en += b.en;
    a.de += b.de;
    a.fr += b.fr;
    return a;
}

// clarity: 0=Obscure, 1=Ambiguous, 2=Clear; falls back toward clear when a tier has no variants.
// The RNG draw always runs on `entry`, so the stream is identical with or without `mask`.
inline Tri PickTemplate(const nlohmann::json& entry, int clarity, CwRng& rng, const nlohmann::json* mask = nullptr) {
    const nlohmann::json& src = mask ? *mask : entry;
    auto render = [&](const char* tier, size_t idx) -> Tri {
        if (tier) {
            auto a = src.value(tier, nlohmann::json::array());
            if (!a.empty())
                return FromJson(a[idx % a.size()]);
        }
        return FromJson(src.value("clear", nlohmann::json::object()));
    };
    if (clarity <= 0) {
        auto obs = entry.value("obscure", nlohmann::json::array());
        if (!obs.empty())
            return render("obscure", obs.size() > 1 ? rng.below(static_cast<uint32_t>(obs.size())) : 0);
    }
    if (clarity <= 1) {
        auto amb = entry.value("ambiguous", nlohmann::json::array());
        if (!amb.empty())
            return render("ambiguous", amb.size() > 1 ? rng.below(static_cast<uint32_t>(amb.size())) : 0);
    }
    return render(nullptr, 0);
}

inline const nlohmann::json* MaskOrNull(const nlohmann::json& j) {
    return j.is_null() ? nullptr : &j;
}

inline void ReplacePlaceholder(Tri& text, int n, const Tri& value) {
    const std::string marker = "[[" + std::to_string(n) + "]]";
    auto rep = [&](std::string& s, const std::string& v) {
        size_t p;
        while ((p = s.find(marker)) != std::string::npos)
            s.replace(p, marker.size(), v);
    };
    rep(text.en, value.en);
    rep(text.de, value.de);
    rep(text.fr, value.fr);
}

struct HintCandidate {
    GameId checkGame;
    std::string checkName;
    std::string areaKey;
    bool dungeon = false, overworld = false, song = false;
    nlohmann::json locationHint = nlohmann::json::object();
    GameId itemGame;
    std::string itemKey;
    nlohmann::json itemHint = nlohmann::json::object();
    nlohmann::json itemHintMask;
    uint32_t weight = 1;
    bool required = false;
    // False for confined or non-shuffled placements, which stones must never target.
    bool hintable = true;
};

struct DistCategory {
    std::string name;
    uint32_t weight;
};
struct Preset {
    uint8_t alwaysCopies;
    uint8_t trialCopies;
    uint32_t junkWeight;
    std::vector<DistCategory> cats;
};
inline const std::array<Preset, 4>& HintPresets() {
    static const std::array<Preset, 4> kPresets{ {
        { 0, 0, 1, {} },
        { 1,
          1,
          6,
          { { "WotH", 7 },
            { "Foolish", 4 },
            { "Song", 2 },
            { "Overworld", 4 },
            { "Dungeon", 3 },
            { "NamedItem", 10 },
            { "Random", 12 } } },
        { 2,
          1,
          0,
          { { "WotH", 12 },
            { "Foolish", 12 },
            { "Song", 4 },
            { "Overworld", 6 },
            { "Dungeon", 6 },
            { "NamedItem", 8 },
            { "Random", 8 } } },
        { 2,
          1,
          0,
          { { "WotH", 15 },
            { "Foolish", 15 },
            { "Song", 2 },
            { "Overworld", 7 },
            { "Dungeon", 7 },
            { "NamedItem", 5 } } },
    } };
    return kPresets;
}

inline constexpr const char* kGanondorfHintKey = "__GANONDORF__";
inline constexpr const char* kStaticHintPrefix = "__STATIC__";

// All randomness derives from masterSeed through an XOR tag, so the fill's own RNG stream is unaffected.
inline nlohmann::json Generate(uint32_t masterSeed, const std::string& sohDumpJson, const std::string& sohHintDumpJson,
                               const std::string& mmDumpJson, const nlohmann::json& foreignArray,
                               const std::string& spoilerJson, const RequirednessResult& pareDown) {
    nlohmann::json out;
    out["version"] = 1;
    nlohmann::json ootHints = nlohmann::json::array();
    nlohmann::json mmGossipPool = nlohmann::json::array();
    nlohmann::json mmItemLocations = nlohmann::json::object();

    CwRng rng(masterSeed ^ 0x48494E54u);

    nlohmann::json hintDump, staticDump, mmDump;
    try {
        hintDump = nlohmann::json::parse(sohHintDumpJson);
    } catch (...) { hintDump = nlohmann::json::object(); }
    try {
        staticDump = nlohmann::json::parse(sohDumpJson);
    } catch (...) { staticDump = nlohmann::json::object(); }
    try {
        mmDump = nlohmann::json::parse(mmDumpJson);
    } catch (...) { mmDump = nlohmann::json::object(); }

    const auto options = hintDump.value("options", nlohmann::json::object());
    const int gossipStoneHints = options.value("gossipStoneHints", 0);
    const int hintClarity = options.value("hintClarity", 2);
    const int hintDistribution = std::clamp(options.value("hintDistribution", 1), 0, 3);
    const bool ganondorfHintOn = options.value("ganondorfHint", 0) != 0;

    struct OotCheckInfo {
        std::string area;
        bool dungeon = false, overworld = false, song = false;
        nlohmann::json locationHint;
    };
    std::unordered_map<std::string, OotCheckInfo> ootChecks;
    for (auto& c : hintDump.value("checks", nlohmann::json::array())) {
        OotCheckInfo info;
        info.area = c.value("area", "");
        info.dungeon = c.value("dungeon", false);
        info.overworld = c.value("overworld", false);
        info.song = c.value("song", false);
        info.locationHint = c.value("locationHint", nlohmann::json::object());
        ootChecks.emplace(c.value("name", ""), std::move(info));
    }
    std::unordered_map<std::string, nlohmann::json> ootItemHints;
    for (auto& it : hintDump.value("items", nlohmann::json::array()))
        ootItemHints.emplace(it.value("name", ""), it.value("hint", nlohmann::json::object()));
    const auto hintTextTable = hintDump.value("hintTextTable", nlohmann::json::object());
    auto tmpl = [&](const char* key) { return hintTextTable.value(key, nlohmann::json::object()); };

    std::unordered_map<std::string, std::string> mmLocationHints;
    const auto mmLocHintsJson = mmDump.value("locationHints", nlohmann::json::object());
    for (auto& [chk, region] : mmLocHintsJson.items())
        mmLocationHints.emplace(chk, region.get<std::string>());
    struct MmItemInfo {
        std::string displayName;
        uint32_t weightClass = 1;
    };
    std::unordered_map<std::string, MmItemInfo> mmItems;
    for (auto& it : mmDump.value("items", nlohmann::json::array())) {
        MmItemInfo info;
        info.displayName = it.value("displayName", it.value("name", ""));
        info.weightClass = it.value("weightClass", 1u);
        mmItems.emplace(it.value("name", ""), std::move(info));
    }
    std::unordered_map<std::string, std::string> ootDisplayNames;
    for (auto& it : staticDump.value("items", nlohmann::json::array())) {
        ootDisplayNames.emplace(it.value("name", ""), it.value("displayName", it.value("name", "")));
    }
    auto clearHint = [](const std::string& name) {
        return nlohmann::json{ { "clear", { { "en", name }, { "de", name }, { "fr", name } } } };
    };

    // Mod items have no hint text of their own in OoT's dump, so they are named by their display name.
    auto itemHintAndWeight = [&](GameId itemGame, const std::string& itemKey,
                                 bool required) -> std::pair<nlohmann::json, uint32_t> {
        if (itemGame == GAME_OOT) {
            auto it = ootItemHints.find(itemKey);
            nlohmann::json hint = it != ootItemHints.end() ? it->second : nlohmann::json::object();
            if (hint.value("clear", nlohmann::json::object()).empty()) {
                auto name = ootDisplayNames.find(itemKey);
                hint["clear"] = clearHint(name != ootDisplayNames.end() ? name->second : itemKey).at("clear");
            }
            return { hint, required ? 3u : 1u };
        }
        auto it = mmItems.find(itemKey);
        std::string dn = it != mmItems.end() ? it->second.displayName : itemKey;
        return { clearHint(dn), it != mmItems.end() ? std::max<uint32_t>(1, it->second.weightClass) : 1u };
    };

    // Hint text must name a trap's disguise, never the trap itself.
    std::unordered_map<std::string, std::string> disguiseByCheck;
    for (auto& fm : foreignArray) {
        std::string fake = fm.value("fakeItemName", "");
        if (!fake.empty())
            disguiseByCheck.emplace(fm.value("checkGame", "") + ":" + fm.value("checkName", ""), std::move(fake));
    }
    auto maskHint = [&](GameId checkGame, const std::string& check, GameId itemGame) -> nlohmann::json {
        auto it = disguiseByCheck.find((checkGame == GAME_OOT ? "oot:" : "mm:") + check);
        if (it == disguiseByCheck.end())
            return nlohmann::json();
        return itemHintAndWeight(itemGame, it->second, false).first;
    };

    for (auto& fm : foreignArray) {
        if (fm.value("checkGame", "") != "oot" || fm.value("itemGame", "") != "mm")
            continue;
        std::string itemName = fm.value("itemName", "");
        std::string checkName = fm.value("checkName", "");
        auto ca = ootChecks.find(checkName);
        std::string area = (ca != ootChecks.end() && !ca->second.area.empty()) ? ca->second.area : checkName;
        if (!itemName.empty())
            mmItemLocations[itemName] = "in " + area + " (OOT)";
    }

    auto placements = ParseSpoilerPlacements(spoilerJson, sohDumpJson, mmDumpJson);
    // Unfiltered by advancement: always-hint checks may hold non-advancement items.
    std::unordered_map<std::string, size_t> ootPlacementIndex;
    for (size_t i = 0; i < placements.size(); ++i)
        if (placements[i].checkGame == GAME_OOT)
            ootPlacementIndex.emplace(placements[i].check, i);
    std::unordered_set<std::string> nonHintable;
    auto loadNonHintable = [&](const char* prefix, const nlohmann::json& dump) {
        const auto fixedArr = dump.value("fixed", nlohmann::json::array());
        bool sawFlag = false;
        for (auto& f : fixedArr) {
            if (f.contains("hintable"))
                sawFlag = true;
            if (!f.value("hintable", true))
                nonHintable.insert(prefix + f.value("check", std::string()));
        }
        if (!fixedArr.empty() && !sawFlag)
            std::cerr << "[HINTS] " << (prefix[0] == 'o' ? "oot" : "mm")
                      << " dump has no 'hintable' flag on fixed placements; hints may "
                         "target non-shuffled checks - rebuild soh.dll/2ship.dll\n";
    };
    loadNonHintable("oot:", staticDump);
    loadNonHintable("mm:", mmDump);
    try {
        for (auto& sk : nlohmann::json::parse(spoilerJson).value("startKnown", nlohmann::json::array()))
            nonHintable.insert(sk.value("checkGame", "") + ":" + sk.value("checkName", ""));
    } catch (...) {}

    std::vector<HintCandidate> candidates;
    candidates.reserve(placements.size());
    std::unordered_set<std::string> areaHasMajor;
    for (auto& p : placements) {
        if (!p.advancement)
            continue;
        HintCandidate c;
        c.checkGame = p.checkGame;
        c.checkName = p.check;
        c.itemGame = p.itemGame;
        c.itemKey = p.item;
        std::string checkKey = (p.checkGame == GAME_OOT ? "oot:" : "mm:") + p.check;
        auto reqIt = pareDown.requiredByCheck.find(checkKey);
        c.required = reqIt != pareDown.requiredByCheck.end() && reqIt->second;
        c.hintable = !nonHintable.count(checkKey);
        if (p.checkGame == GAME_OOT) {
            auto it = ootChecks.find(p.check);
            if (it != ootChecks.end()) {
                c.areaKey = "oot:" + it->second.area;
                c.dungeon = it->second.dungeon;
                c.overworld = it->second.overworld;
                c.song = it->second.song;
                c.locationHint = it->second.locationHint;
            }
        } else {
            auto it = mmLocationHints.find(p.check);
            std::string region = it != mmLocationHints.end() ? it->second : p.check;
            c.areaKey = "mm:" + region;
            c.overworld = true;
            c.locationHint = { { "clear", { { "en", region }, { "de", region }, { "fr", region } } } };
        }
        std::tie(c.itemHint, c.weight) = itemHintAndWeight(p.itemGame, p.item, c.required);
        c.itemHintMask = maskHint(p.checkGame, p.check, p.itemGame);
        if (p.major && !c.areaKey.empty())
            areaHasMajor.insert(c.areaKey);
        candidates.push_back(std::move(c));
    }

    // WotH counts only hintable required checks; areaHasMajor stays unfiltered to match native barren logic.
    std::unordered_set<std::string> areaHasHintableRequired;
    for (auto& c : candidates)
        if (c.hintable && c.required && !c.areaKey.empty())
            areaHasHintableRequired.insert(c.areaKey);
    std::vector<std::string> requiredAreas, foolishAreas;
    for (auto& entry : pareDown.areaHasRequired) {
        const std::string& key = entry.first;
        if (key.find("Unknown") != std::string::npos)
            continue;
        if (areaHasHintableRequired.count(key))
            requiredAreas.push_back(key);
        else if (!areaHasMajor.count(key))
            foolishAreas.push_back(key);
    }
    // Sorted so RNG picks never depend on unordered_map bucket order.
    std::sort(requiredAreas.begin(), requiredAreas.end());
    std::sort(foolishAreas.begin(), foolishAreas.end());

    std::unordered_set<std::string> usedCheckKeys, usedAreaKeys;
    auto areaText = [&](const std::string& areaKey) -> Tri {
        size_t colon = areaKey.find(':');
        std::string plain = colon == std::string::npos ? areaKey : areaKey.substr(colon + 1);
        // MM region strings arrive prefixed with "in ", which the templates already supply.
        if (areaKey.rfind("mm:", 0) == 0 && plain.rfind("in ", 0) == 0)
            plain = plain.substr(3);
        return EnglishOnly(plain);
    };

    // nullopt means the item is in no check, so no hint may name a location for it.
    auto findOotItem = [&](const std::string& name) {
        return std::find_if(placements.begin(), placements.end(),
                            [&](const CwPlacedItem& p) { return p.itemGame == GAME_OOT && p.item == name; });
    };
    auto placementAreaText = [&](std::vector<CwPlacedItem>::const_iterator it, bool yourPocket) -> std::optional<Tri> {
        if (it->checkGame != GAME_OOT) {
            auto mk = mmLocationHints.find(it->check);
            return areaText("mm:" + (mk != mmLocationHints.end() ? mk->second : it->check));
        }
        if (yourPocket && it->check == "Link's Pocket")
            return PickTemplate(tmpl("RHT_YOUR_POCKET"), hintClarity, rng);
        auto ck = ootChecks.find(it->check);
        if (ck == ootChecks.end())
            return std::nullopt;
        return areaText("oot:" + ck->second.area);
    };
    auto itemAreaText = [&](const char* itemName, bool yourPocket = false) -> std::optional<Tri> {
        auto it = findOotItem(itemName);
        // The pool may carry only the progressive chain that grants the concrete item a hint names.
        if (it == placements.end())
            it = findOotItem("Progressive " + std::string(itemName));
        if (it == placements.end())
            return std::nullopt;
        return placementAreaText(it, yourPocket);
    };

    auto pickUnused = [&](const std::vector<size_t>& idxs, const std::vector<std::string>& keys,
                          std::unordered_set<std::string>& used) -> int {
        std::vector<size_t> avail;
        for (size_t i : idxs)
            if (!used.count(keys[i]))
                avail.push_back(i);
        if (avail.empty())
            return -1;
        return static_cast<int>(avail[rng.below(static_cast<uint32_t>(avail.size()))]);
    };

    size_t totalStones = hintDump.value("stones", nlohmann::json::array()).size();
    int producedHints = 0, producedJunk = 0;

    const Preset& preset = HintPresets()[hintDistribution];
    if (gossipStoneHints != 0 && preset.trialCopies > 0) {
        for (auto& trialName : hintDump.value("requiredTrials", nlohmann::json::array())) {
            if (totalStones == 0)
                break;
            std::string name = trialName.get<std::string>();
            std::string sentence = name + " is required to reach Ganon's Castle.";
            sentence[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(sentence[0])));
            Tri msg = EnglishOnly(sentence);
            for (uint8_t copy = 0; copy < preset.trialCopies && totalStones > 0; ++copy, --totalStones) {
                ootHints.push_back({ { "checkName", "__TRIAL__" + name + std::to_string(copy) },
                                     { "type", "trial" },
                                     { "messages", { { { "en", msg.en }, { "de", msg.de }, { "fr", msg.fr } } } } });
                ++producedHints;
            }
        }
    }

    // The native builder picks the message by index, so a shuffled Master Sword needs all three variants in order.
    if (ganondorfHintOn) {
        const std::optional<Tri> laArea = itemAreaText("Light Arrows", true);
        const std::optional<Tri> msArea = itemAreaText("Master Sword", true);
        const bool msShuffled =
            options.value("shuffleMasterSword", 0) != 0 && options.value("startingMasterSword", 0) == 0;
        // An unresolvable sword must skip the whole hint, since native reads index 1/2 whenever it is shuffled.
        if (laArea && (!msShuffled || msArea)) {
            auto render = [&](const char* key) {
                Tri m = PickTemplate(tmpl(key), hintClarity, rng);
                ReplacePlaceholder(m, 1, *laArea);
                if (msArea)
                    ReplacePlaceholder(m, 2, *msArea);
                return m;
            };
            nlohmann::json msgs = nlohmann::json::array();
            auto push = [&](const Tri& m) { msgs.push_back({ { "en", m.en }, { "de", m.de }, { "fr", m.fr } }); };
            push(render("RHT_GANONDORF_HINT_LA_ONLY"));
            if (msShuffled) {
                push(render("RHT_GANONDORF_HINT_MS_ONLY"));
                push(render("RHT_GANONDORF_HINT_LA_AND_MS"));
            }
            ootHints.push_back(
                { { "checkName", kGanondorfHintKey }, { "type", "ganondorf" }, { "messages", std::move(msgs) } });
        }
    }

    // Native only searches OOT's own checks, so an item cross-placed into MM would render as "an Isolated Place".
    // These templates have no clarity variants, so this block draws nothing from the RNG.
    {
        struct StaticItemHint {
            const char* hint;
            const char* option;
            std::vector<const char*> templates;
            const char* item;
            bool yourPocket;
            const char* mmTwin = nullptr;
        };
        static const std::vector<StaticItemHint> kStaticItemHints = {
            { "RH_SHEIK_HINT", "sheikLaHint", { "RHT_SHEIK_HINT_LA_ONLY" }, "Light Arrows", true },
            { "RH_FOREST_BOSS_KEY_HINT", "bossKeyHint", { "RHT_BOSS_KEY_HINT" }, "Forest Temple Boss Key", true },
            { "RH_FIRE_BOSS_KEY_HINT", "bossKeyHint", { "RHT_BOSS_KEY_HINT" }, "Fire Temple Boss Key", true },
            { "RH_WATER_BOSS_KEY_HINT", "bossKeyHint", { "RHT_BOSS_KEY_HINT" }, "Water Temple Boss Key", true },
            { "RH_SPIRIT_BOSS_KEY_HINT", "bossKeyHint", { "RHT_BOSS_KEY_HINT" }, "Spirit Temple Boss Key", true },
            { "RH_SHADOW_BOSS_KEY_HINT", "bossKeyHint", { "RHT_BOSS_KEY_HINT" }, "Shadow Temple Boss Key", true },
            { "RH_GANONS_BOSS_KEY_HINT", "bossKeyHint", { "RHT_BOSS_KEY_HINT" }, "Ganon's Castle Boss Key", true },
            { "RH_DAMPES_DIARY", "dampesDiaryHint", { "RHT_DAMPE_DIARY" }, "Progressive Hookshot", false },
            { "RH_GREG_RUPEE", "gregHint", { "RHT_GREG_HINT" }, "Greg the Green Rupee", false },
            { "RH_SARIA_HINT",
              "sariaHint",
              { "RHT_SARIA_TALK_HINT", "RHT_SARIA_SONG_HINT" },
              "Progressive Magic Meter",
              true,
              "Progressive Magic" },
            { "RH_MIDO_HINT", "midoHint", { "RHT_MIDO_HINT" }, "Kokiri Sword", true },
            { "RH_FISHING_POLE", "fishingPoleHint", { "RHT_FISHING_POLE_HINT" }, "Fishing Pole", true },
        };
        for (const StaticItemHint& sh : kStaticItemHints) {
            if (options.value(sh.option, 0) == 0)
                continue;
            auto it = findOotItem(sh.item);
            std::optional<Tri> area = itemAreaText(sh.item, sh.yourPocket);
            if (it == placements.end() && sh.mmTwin != nullptr) {
                it = std::find_if(placements.begin(), placements.end(),
                                  [&](const CwPlacedItem& p) { return p.itemGame == GAME_MM && p.item == sh.mmTwin; });
                area = it != placements.end() ? placementAreaText(it, sh.yourPocket) : std::nullopt;
            }
            if (it == placements.end() || !area)
                continue;
            nlohmann::json msgs = nlohmann::json::array();
            for (const char* key : sh.templates) {
                Tri m = PickTemplate(tmpl(key), hintClarity, rng);
                ReplacePlaceholder(m, 1, *area);
                msgs.push_back({ { "en", m.en }, { "de", m.de }, { "fr", m.fr } });
            }
            ootHints.push_back({ { "checkName", std::string(kStaticHintPrefix) + sh.hint },
                                 { "type", "static" },
                                 { "messages", std::move(msgs) } });
            // Reserved from the weighted loop so stones never re-target it.
            usedCheckKeys.insert((it->checkGame == GAME_OOT ? "oot:" : "mm:") + it->check);
        }
    }

    // Native only searches OOT's own checks, which would leave a literal "[[N]]" for a reward placed in MM.
    const bool compassInformation = options.value("mapsCompassesGiveInformation", 0) != 0;
    if (options.value("totAltarHint", 0) != 0 || compassInformation) {
        auto rewardArea = [&](const char* itemName) -> Tri {
            return itemAreaText(itemName).value_or(EnglishOnly("an unknown place"));
        };
        auto endClause = [&](const char* jsonKey) -> Tri {
            std::string key = options.value(jsonKey, "");
            return key.empty() ? Tri{} : PickTemplate(tmpl(key.c_str()), hintClarity, rng);
        };
        auto insertNumber = [](std::string& s, int num) {
            size_t bar1 = s.find('|');
            if (bar1 != std::string::npos) {
                size_t bar2 = s.find('|', bar1 + 1);
                size_t bar3 = bar2 == std::string::npos ? std::string::npos : s.find('|', bar2 + 1);
                if (bar3 != std::string::npos) {
                    if (num == 1)
                        s.erase(bar2, bar3 - bar2);
                    else
                        s.erase(bar1, bar2 - bar1);
                }
            }
            size_t p;
            while ((p = s.find('|')) != std::string::npos)
                s.erase(p, 1);
            while ((p = s.find("[[d]]")) != std::string::npos)
                s.replace(p, 5, std::to_string(num));
        };
        auto withCount = [&](const char* templateKey, const char* countKey) -> Tri {
            std::string key = options.value(templateKey, "");
            if (key.empty())
                return {};
            Tri t = PickTemplate(tmpl(key.c_str()), hintClarity, rng);
            int count = options.value(countKey, 0);
            insertNumber(t.en, count);
            insertNumber(t.de, count);
            insertNumber(t.fr, count);
            return t;
        };

        static const char* kChildRewards[3] = { "Kokiri's Emerald", "Goron's Ruby", "Zora's Sapphire" };
        Tri childMsg;
        if (!compassInformation) {
            childMsg = PickTemplate(tmpl("RHT_CHILD_ALTAR_STONES"), hintClarity, rng);
            for (int i = 0; i < 3; ++i)
                ReplacePlaceholder(childMsg, i + 1, rewardArea(kChildRewards[i]));
        }
        childMsg += endClause("doorOfTimeTemplate");
        ootHints.push_back(
            { { "checkName", "__ALTAR_CHILD__" },
              { "type", "altarChild" },
              { "messages", { { { "en", childMsg.en }, { "de", childMsg.de }, { "fr", childMsg.fr } } } } });

        static const char* kAdultRewards[6] = { "Light Medallion", "Forest Medallion", "Fire Medallion",
                                                "Water Medallion", "Spirit Medallion", "Shadow Medallion" };
        Tri adultMsg;
        if (!compassInformation) {
            adultMsg = PickTemplate(tmpl("RHT_ADULT_ALTAR_MEDALLIONS"), hintClarity, rng);
            for (int i = 0; i < 6; ++i)
                ReplacePlaceholder(adultMsg, i + 1, rewardArea(kAdultRewards[i]));
        }
        adultMsg += withCount("bridgeTemplate", "bridgeCount");
        adultMsg += withCount("gbkTemplate", "gbkCount");
        adultMsg += withCount("soulTemplate", "soulCount");
        adultMsg += withCount("winconTemplate", "winconCount");
        adultMsg += PickTemplate(tmpl("RHT_ADULT_ALTAR_TEXT_END"), hintClarity, rng);
        ootHints.push_back(
            { { "checkName", "__ALTAR_ADULT__" },
              { "type", "altarAdult" },
              { "messages", { { { "en", adultMsg.en }, { "de", adultMsg.de }, { "fr", adultMsg.fr } } } } });
    }

    if (gossipStoneHints != 0) {
        // Placed before the weighted loop so it cannot re-target these checks.
        if (preset.alwaysCopies > 0) {
            for (auto& an : hintDump.value("alwaysHintChecks", nlohmann::json::array())) {
                if (totalStones == 0)
                    break;
                std::string checkName = an.get<std::string>();
                std::string checkKey = "oot:" + checkName;
                if (usedCheckKeys.count(checkKey) || nonHintable.count(checkKey))
                    continue;
                auto locIt = ootChecks.find(checkName);
                auto plIt = ootPlacementIndex.find(checkName);
                if (locIt == ootChecks.end() || plIt == ootPlacementIndex.end())
                    continue;
                const auto& placed = placements[plIt->second];
                nlohmann::json itemHint = itemHintAndWeight(placed.itemGame, placed.item, false).first;
                // This check can hold a disguised trap, so hint the disguise.
                nlohmann::json itemMask = maskHint(GAME_OOT, checkName, placed.itemGame);
                Tri msg = PickTemplate(locIt->second.locationHint, hintClarity, rng);
                ReplacePlaceholder(msg, 1, PickTemplate(itemHint, hintClarity, rng, MaskOrNull(itemMask)));
                uint8_t copies = std::min<uint8_t>(preset.alwaysCopies, static_cast<uint8_t>(totalStones));
                for (uint8_t copy = 0; copy < copies; ++copy) {
                    ootHints.push_back(
                        { { "checkName", "__ALWAYS__" + checkName + std::to_string(copy) },
                          { "type", "always" },
                          { "messages", { { { "en", msg.en }, { "de", msg.de }, { "fr", msg.fr } } } } });
                    ++producedHints;
                }
                totalStones -= copies;
                usedCheckKeys.insert(checkKey);
            }
        }

        for (auto& hn : hintDump.value("hintAccessibleChecks", nlohmann::json::array()))
            usedCheckKeys.insert("oot:" + hn.get<std::string>());

        std::vector<DistCategory> dist = preset.cats;
        while (totalStones > 0) {
            uint32_t totalWeight = 0;
            for (auto& d : dist)
                totalWeight += d.weight;
            if (totalWeight == 0)
                break;
            uint32_t roll = totalWeight <= 1 ? 1 : (rng.below(totalWeight) + 1);
            uint32_t cursor = 0;
            size_t chosen = dist.size();
            for (size_t i = 0; i < dist.size(); ++i) {
                cursor += dist[i].weight;
                if (roll <= cursor) {
                    chosen = i;
                    break;
                }
            }
            if (chosen == dist.size())
                break;
            const std::string& cat = dist[chosen].name;
            Tri msg;
            bool placed = false;
            std::string usedKey;

            if (cat == "WotH" || cat == "Foolish") {
                auto& pool = (cat == "WotH") ? requiredAreas : foolishAreas;
                std::vector<size_t> idxs(pool.size());
                for (size_t i = 0; i < pool.size(); ++i)
                    idxs[i] = i;
                int pick = pickUnused(idxs, pool, usedAreaKeys);
                if (pick >= 0) {
                    usedKey = pool[pick];
                    msg = PickTemplate(tmpl(cat == "WotH" ? "RHT_WAY_OF_THE_HERO" : "RHT_FOOLISH"), hintClarity, rng);
                    ReplacePlaceholder(msg, 1, areaText(usedKey));
                    usedAreaKeys.insert(usedKey);
                    placed = true;
                }
            } else if (cat == "Song" || cat == "Overworld" || cat == "Dungeon") {
                std::vector<size_t> idxs;
                std::vector<std::string> keys(candidates.size());
                for (size_t i = 0; i < candidates.size(); ++i) {
                    keys[i] = (candidates[i].checkGame == GAME_OOT ? "oot:" : "mm:") + candidates[i].checkName;
                    bool eligible = candidates[i].hintable && ((cat == "Song" && candidates[i].song) ||
                                                               (cat == "Overworld" && candidates[i].overworld) ||
                                                               (cat == "Dungeon" && candidates[i].dungeon));
                    if (eligible)
                        idxs.push_back(i);
                }
                int pick = pickUnused(idxs, keys, usedCheckKeys);
                if (pick >= 0) {
                    usedKey = keys[pick];
                    const auto& cand = candidates[pick];
                    if (cand.checkGame == GAME_OOT) {
                        msg = PickTemplate(cand.locationHint, hintClarity, rng);
                        ReplacePlaceholder(
                            msg, 1, PickTemplate(cand.itemHint, hintClarity, rng, MaskOrNull(cand.itemHintMask)));
                    } else {
                        msg = PickTemplate(tmpl("RHT_CAN_BE_FOUND_AT"), hintClarity, rng);
                        ReplacePlaceholder(
                            msg, 1, PickTemplate(cand.itemHint, hintClarity, rng, MaskOrNull(cand.itemHintMask)));
                        ReplacePlaceholder(msg, 2, areaText(cand.areaKey));
                    }
                    usedCheckKeys.insert(usedKey);
                    placed = true;
                }
            } else if (cat == "NamedItem" || cat == "Random") {
                std::vector<size_t> idxs;
                std::vector<std::string> keys(candidates.size());
                for (size_t i = 0; i < candidates.size(); ++i) {
                    keys[i] = (candidates[i].checkGame == GAME_OOT ? "oot:" : "mm:") + candidates[i].checkName;
                    if (candidates[i].hintable && (cat == "Random" || candidates[i].required))
                        idxs.push_back(i);
                }
                int pick = pickUnused(idxs, keys, usedCheckKeys);
                if (pick >= 0) {
                    usedKey = keys[pick];
                    const auto& cand = candidates[pick];
                    msg = PickTemplate(tmpl(cand.dungeon ? "RHT_HOARDS" : "RHT_CAN_BE_FOUND_AT"), hintClarity, rng);
                    ReplacePlaceholder(msg, 1,
                                       PickTemplate(cand.itemHint, hintClarity, rng, MaskOrNull(cand.itemHintMask)));
                    ReplacePlaceholder(msg, 2, areaText(cand.areaKey));
                    usedCheckKeys.insert(usedKey);
                    placed = true;
                }
            }

            if (!placed) {
                dist[chosen].weight = 0;
                continue;
            }
            ootHints.push_back({ { "checkName", "__STONE__" + std::to_string(producedHints) },
                                 { "type", cat },
                                 { "messages", { { { "en", msg.en }, { "de", msg.de }, { "fr", msg.fr } } } } });
            ++producedHints;
            --totalStones;
        }

        auto junkTemplates = nlohmann::json::array();
        for (auto& [key, val] : hintTextTable.items())
            if (key.rfind("RHT_JUNK", 0) == 0)
                junkTemplates.push_back(val);
        for (; totalStones > 0; --totalStones) {
            Tri msg = junkTemplates.empty()
                          ? EnglishOnly("They say that this and that are related.")
                          : PickTemplate(junkTemplates[rng.below(static_cast<uint32_t>(junkTemplates.size()))], 2, rng);
            ootHints.push_back({ { "checkName", "__JUNK__" + std::to_string(producedJunk) },
                                 { "type", "junk" },
                                 { "messages", { { { "en", msg.en }, { "de", msg.de }, { "fr", msg.fr } } } } });
            ++producedJunk;
        }
    }

    // MM's native stone draw cannot see OOT checks, so every advancement item on one is pooled here.
    for (auto& c : candidates) {
        if (c.checkGame != GAME_OOT || !c.hintable)
            continue;
        Tri itemName = PickTemplate(c.itemHint, 2, rng, MaskOrNull(c.itemHintMask));
        Tri area = areaText(c.areaKey);
        std::string text = itemName.en + " can be found " + (c.dungeon ? "hoarded in " : "at ") + area.en + " (OOT)";
        mmGossipPool.push_back({ { "weight", c.required ? 3 : 1 }, { "text", text } });
    }

    nlohmann::json ootItemAreas = nlohmann::json::object();
    for (auto& p : placements) {
        if (p.itemGame != GAME_OOT || ootItemAreas.contains(p.item))
            continue;
        std::string area;
        if (p.checkGame == GAME_OOT) {
            auto ck = ootChecks.find(p.check);
            if (ck != ootChecks.end() && !ck->second.area.empty())
                area = areaText("oot:" + ck->second.area).en;
        } else {
            auto mk = mmLocationHints.find(p.check);
            area = areaText("mm:" + (mk != mmLocationHints.end() ? mk->second : p.check)).en;
        }
        if (!area.empty())
            ootItemAreas[p.item] = area;
    }

    out["oot"] = std::move(ootHints);
    out["ootItemAreas"] = std::move(ootItemAreas);
    out["mm"] = { { "gossipPool", std::move(mmGossipPool) }, { "itemLocations", std::move(mmItemLocations) } };
    out["stats"] = { { "hintsProduced", producedHints }, { "junkProduced", producedJunk } };
    return out;
}

} // namespace ComboRando
