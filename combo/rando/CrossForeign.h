#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <nlohmann/json.hpp>

#include "SharedItems.h"

namespace ComboRando {

enum GameId : uint8_t { GAME_OOT = 0, GAME_MM = 1 };

// fileNum is 0-based (MM maps its 1-based file number via fileNum = mmFileNum - 1).
typedef const char* (*FnComboReadSave)(int game, int fileNum);
typedef void (*FnComboWriteSave)(int game, int fileNum, const char* json);

inline constexpr const char* kForeignSentinelNameOOT = "Combo Foreign Item";
inline constexpr const char* kForeignSentinelNameMM = "RI_COMBO_FOREIGN";

// MM's apply throws on a real foreign item, so the sentinel goes back at every foreign check.
inline nlohmann::json ApplyPayloadFromConsolidated(const nlohmann::json& consolidated, GameId game) {
    const char* gameKey = (game == GAME_OOT) ? "oot" : "mm";
    const char* sentinel = (game == GAME_OOT) ? kForeignSentinelNameOOT : kForeignSentinelNameMM;
    nlohmann::json apply =
        consolidated.value(gameKey, nlohmann::json::object()).value("placements", nlohmann::json::object());
    for (const auto& fm : consolidated.value("foreign", nlohmann::json::array())) {
        std::string checkName = fm.value("checkName", "");
        if (!checkName.empty() && fm.value("checkGame", "") == gameKey) {
            apply[checkName] = sentinel;
        }
    }
    if (game == GAME_OOT) {
        const nlohmann::json oot = consolidated.value("oot", nlohmann::json::object());
        if (oot.contains("iceTrapModels")) {
            apply["__iceTrapModels"] = oot["iceTrapModels"];
        }
    }
    return apply;
}

struct ForeignItem {
    GameId itemGame;
    std::string itemName;
    std::string displayName;
    bool advancement = false;
    bool trap = false;
    std::string category;
    std::string fakeItemName;
    std::string fakeDisplayName;
    std::string fakeTrickName;
    bool shared = false;
    bool HasDisguise() const {
        return !fakeItemName.empty();
    }
};

inline std::string GameIdToKey(GameId g) {
    return g == GAME_OOT ? "oot" : "mm";
}
inline GameId KeyToGameId(const std::string& s) {
    return s == "mm" ? GAME_MM : GAME_OOT;
}

inline std::filesystem::path ConsolidatedDir() {
    return std::filesystem::path("Randomizer");
}

inline std::filesystem::path ComboSpoilerPath(const nlohmann::json& fileHash, const char* stem = "Combo") {
    std::string name = stem;
    for (const auto& idx : fileHash) {
        char pair[8];
        snprintf(pair, sizeof(pair), "-%02d", idx.get<int>());
        name += pair;
    }
    return ConsolidatedDir() / (name + ".json");
}

inline std::string g_comboForeignJson;

inline void Combo_SetForeignJson(const char* json) {
    g_comboForeignJson = json ? json : "";
}

inline std::string StripGameSuffix(std::string s) {
    for (const char* suf : { " (OOT)", " (MM)" }) {
        const size_t n = std::strlen(suf);
        if (s.size() > n && s.compare(s.size() - n, n, suf) == 0) {
            s.resize(s.size() - n);
            break;
        }
    }
    return s;
}

inline std::string MakeTrickName(const std::string& name, uint32_t r) {
    if (name.empty())
        return name;
    std::string out = name;
    const size_t i = r % out.size();
    out.insert(i, 1, out[i]);
    return out;
}

inline std::string MakeVowelSwapName(const std::string& name, uint32_t r) {
    static constexpr char vowels[] = { 'a', 'e', 'i', 'o', 'u' };
    std::vector<size_t> at;
    for (size_t i = 0; i < name.size(); ++i) {
        if (std::strchr("aeiouAEIOU", name[i]) != nullptr)
            at.push_back(i);
    }
    if (at.empty())
        return name;
    std::string out = name;
    const size_t pos = at[r % at.size()];
    const bool upper = out[pos] >= 'A' && out[pos] <= 'Z';
    const char old = upper ? out[pos] + ('a' - 'A') : out[pos];
    uint32_t idx = (r / static_cast<uint32_t>(at.size())) % 4;
    idx += vowels[idx] >= old;
    out[pos] = upper ? vowels[idx] - ('a' - 'A') : vowels[idx];
    return out;
}

inline std::string MakeDuplicateLetterName(const std::string& name, uint32_t r) {
    std::vector<size_t> at;
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            at.push_back(i);
    }
    if (at.empty())
        return name;
    std::string out = name;
    const size_t pos = at[r % at.size()];
    out.insert(pos + 1, 1, out[pos]);
    return out;
}

struct ForeignItemMeta {
    std::string displayName;
    bool advancement = false;
    bool trap = false;
    std::vector<std::string> trickNames;
};

inline std::unordered_map<std::string, ForeignItemMeta> ParseItemMeta(const std::string& dump) {
    std::unordered_map<std::string, ForeignItemMeta> m;
    try {
        auto d = nlohmann::json::parse(dump);
        for (const auto& it : d.value("items", nlohmann::json::array())) {
            std::string n = it.value("name", "");
            if (n.empty())
                continue;
            ForeignItemMeta meta;
            meta.displayName = it.value("displayName", n);
            meta.advancement = it.value("advancement", false);
            meta.trap = it.value("trap", false);
            meta.trickNames = it.value("trickNames", std::vector<std::string>{});
            m.emplace(std::move(n), std::move(meta));
        }
    } catch (...) {}
    return m;
}

// `present` is false for a stale dump or an empty field, so the caller falls back to its own filter.
inline std::set<std::string> ParseIceTrapModels(const std::string& dump, bool& present) {
    std::set<std::string> out;
    try {
        auto d = nlohmann::json::parse(dump);
        auto it = d.find("iceTrapModels");
        if (it != d.end() && it->is_array()) {
            for (const auto& n : *it) {
                if (n.is_string())
                    out.insert(n.get<std::string>());
            }
        }
    } catch (...) {}
    present = !out.empty();
    return out;
}

// Deterministic: a dedicated LCG stream off masterSeed, sorted candidate sets, array order.
inline void AssignTrapDisguises(nlohmann::json& foreignArr, const nlohmann::json& ootPlacements,
                                const nlohmann::json& mmPlacements, const std::string& sohDump,
                                const std::string& mmDump, uint32_t masterSeed) {
    const auto ootMeta = ParseItemMeta(sohDump), mmMeta = ParseItemMeta(mmDump);
    if (ootMeta.empty() && mmMeta.empty())
        return;
    // The curated list holds representative names that no concrete placement matches, so those drop out too.
    bool curatedPresent = false;
    const std::set<std::string> curated = ParseIceTrapModels(sohDump, curatedPresent);
    int trapNames = 1;
    try {
        trapNames =
            nlohmann::json::parse(sohDump).value("accessibility", nlohmann::json::object()).value("iceTrapNames", 1);
    } catch (...) {}
    std::set<std::string> ootCand, mmCand;
    auto scan = [&](const nlohmann::json& pl) {
        if (!pl.is_object())
            return;
        for (auto it = pl.begin(); it != pl.end(); ++it) {
            if (!it.value().is_string())
                continue;
            const std::string n = it.value().get<std::string>();
            const bool ootOk = curatedPresent ? curated.count(n) != 0 : (n != "Triforce" && n != "Triforce Piece");
            auto o = ootMeta.find(n);
            if (ootOk && o != ootMeta.end() && o->second.advancement && !o->second.trap)
                ootCand.insert(n);
            auto m = mmMeta.find(n);
            if (m != mmMeta.end() && m->second.advancement && !m->second.trap)
                mmCand.insert(n);
        }
    };
    scan(ootPlacements);
    scan(mmPlacements);

    uint64_t s = static_cast<uint64_t>(masterSeed) ^ 0x7A9F3C1D5E2B4867ULL;
    auto next = [&s]() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(s >> 33);
    };
    for (auto& fm : foreignArr) {
        const std::string itemGame = fm.value("itemGame", "");
        if (itemGame != "oot" && itemGame != "mm")
            continue;
        const auto& meta = (itemGame == "mm") ? mmMeta : ootMeta;
        auto mit = meta.find(fm.value("itemName", ""));
        if (mit == meta.end() || !mit->second.trap)
            continue;
        fm["trap"] = true;
        const auto& cand = (itemGame == "mm") ? mmCand : ootCand;
        if (cand.empty())
            continue;
        auto cit = cand.begin();
        std::advance(cit, next() % cand.size());
        const std::string& fake = *cit;
        auto fmeta = meta.find(fake);
        const std::string dn =
            (fmeta != meta.end() && !fmeta->second.displayName.empty()) ? fmeta->second.displayName : fake;
        fm["fakeItemName"] = fake;
        fm["fakeDisplayName"] = dn;
        // Exactly one draw per disguised trap whatever the option keeps other output stable.
        const uint32_t r = next();
        const std::vector<std::string>* tn = (fmeta != meta.end()) ? &fmeta->second.trickNames : nullptr;
        switch (trapNames) {
            case 0:
                fm["fakeTrickName"] = dn;
                break;
            case 2:
                fm["fakeTrickName"] = MakeVowelSwapName(dn, r);
                break;
            case 3:
                fm["fakeTrickName"] = MakeDuplicateLetterName(dn, r);
                break;
            case 4:
                fm["fakeTrickName"] =
                    mit->second.displayName.empty() ? fm.value("itemName", "") : mit->second.displayName;
                break;
            default:
                fm["fakeTrickName"] = (tn != nullptr && !tn->empty()) ? (*tn)[r % tn->size()] : MakeTrickName(dn, r);
                break;
        }
    }
}

inline const char* GameSuffix(GameId g) {
    return g == GAME_MM ? " (MM)" : " (OOT)";
}

inline std::string ShownForeignName(const ForeignItem& fi, const char* resolved) {
    if (resolved != nullptr && resolved[0] != '\0') {
        return fi.shared ? std::string(resolved) : std::string(resolved) + GameSuffix(fi.itemGame);
    }
    return fi.displayName;
}

struct CwSharedPair {
    std::string ootName;
    std::string mmName;
    std::string key;
};

struct NativePairKey {
    GameId game;
    const char* name;
    const char* key;
};

inline constexpr const char* kTriforcePairKey = "fleet.triforce_piece";

inline constexpr NativePairKey kNativePairKeys[] = {
    { GAME_MM, "Pendant of Memories", "nei.equip.pendant" },
    { GAME_OOT, "Cojiro", "nei.equip.cojiro" },
    { GAME_OOT, "Forest Medallion", "nei.medallion.forest" },
    { GAME_OOT, "Fire Medallion", "nei.medallion.fire" },
    { GAME_OOT, "Water Medallion", "nei.medallion.water" },
    { GAME_OOT, "Spirit Medallion", "nei.medallion.spirit" },
    { GAME_OOT, "Shadow Medallion", "nei.medallion.shadow" },
    { GAME_OOT, "Light Medallion", "nei.medallion.light" },
    { GAME_OOT, "Kokiri's Emerald", "nei.spiritual_stone.kokiri" },
    { GAME_OOT, "Goron's Ruby", "nei.spiritual_stone.goron" },
    { GAME_OOT, "Zora's Sapphire", "nei.spiritual_stone.zora" },
    { GAME_OOT, "Infinite Quiver", "nei.infinite.quiver" },
    { GAME_OOT, "Infinite Bomb Bag", "nei.infinite.bomb_bag" },
    { GAME_OOT, "Infinite Stick Capacity", "nei.infinite.deku_sticks" },
    { GAME_OOT, "Infinite Nut Capacity", "nei.infinite.deku_nuts" },
    { GAME_OOT, "Infinite Magic Meter", "nei.infinite.magic" },
    { GAME_OOT, "Infinite Bombchus", "nei.infinite.bombchus" },
    { GAME_OOT, "Infinite Wallet", "nei.infinite.wallet" },
    { GAME_OOT, "Triforce Piece", kTriforcePairKey },
    { GAME_MM, "Piece of the Triforce", kTriforcePairKey },
    { GAME_MM, "Bunny Hood", "nei.mask_bunny" },
    { GAME_MM, "Mask of Truth", "nei.mask_truth" },
    { GAME_MM, "Bomber's Notebook", "nei.combo.bombers_notebook" },
    { GAME_OOT, "Soul of Armos", "fleet.soul.armos" },
    { GAME_MM, "Soul of Armos", "fleet.soul.armos" },
    { GAME_OOT, "Soul of Beamos", "fleet.soul.beamos" },
    { GAME_MM, "Soul of Beamos", "fleet.soul.beamos" },
    { GAME_OOT, "Soul of Bubbles", "fleet.soul.bubble" },
    { GAME_MM, "Soul of Bubbles", "fleet.soul.bubble" },
    { GAME_OOT, "Soul of Deku Babas", "fleet.soul.deku_baba" },
    { GAME_MM, "Soul of Deku Babas", "fleet.soul.deku_baba" },
    { GAME_OOT, "Soul of Dodongos", "fleet.soul.dodongo" },
    { GAME_MM, "Soul of Dodongos", "fleet.soul.dodongo" },
    { GAME_OOT, "Soul of Freezards", "fleet.soul.freezard" },
    { GAME_MM, "Soul of Freezards", "fleet.soul.freezard" },
    { GAME_OOT, "Soul of Gerudo Fighters", "fleet.soul.gerudo_fighter" },
    { GAME_MM, "Soul of Pirates", "fleet.soul.gerudo_fighter" },
    { GAME_OOT, "Soul of Guays", "fleet.soul.guay" },
    { GAME_MM, "Soul of Guays", "fleet.soul.guay" },
    { GAME_OOT, "Soul of Iron Knuckles", "fleet.soul.iron_knuckle" },
    { GAME_MM, "Soul of Iron Knuckles", "fleet.soul.iron_knuckle" },
    { GAME_OOT, "Soul of Keese", "fleet.soul.keese" },
    { GAME_MM, "Soul of Keese", "fleet.soul.keese" },
    { GAME_OOT, "Soul of Leevers", "fleet.soul.leever" },
    { GAME_MM, "Soul of Leevers", "fleet.soul.leever" },
    { GAME_OOT, "Soul of Like Likes", "fleet.soul.like_like" },
    { GAME_MM, "Soul of Like Likes", "fleet.soul.like_like" },
    { GAME_OOT, "Soul of Mad Scrubs", "fleet.soul.mad_scrub" },
    { GAME_MM, "Soul of Mad Scrubs", "fleet.soul.mad_scrub" },
    { GAME_OOT, "Soul of Octoroks", "fleet.soul.octorok" },
    { GAME_MM, "Soul of Octoroks", "fleet.soul.octorok" },
    { GAME_OOT, "Soul of Peahats", "fleet.soul.peahat" },
    { GAME_MM, "Soul of Peahats", "fleet.soul.peahat" },
    { GAME_OOT, "Soul of Poes", "fleet.soul.poe" },
    { GAME_MM, "Soul of Poes", "fleet.soul.poe" },
    { GAME_OOT, "Soul of Redeads", "fleet.soul.redead" },
    { GAME_MM, "Soul of Redeads", "fleet.soul.redead" },
    { GAME_OOT, "Soul of Shellblades", "fleet.soul.shellblade" },
    { GAME_MM, "Soul of Shellblades", "fleet.soul.shellblade" },
    { GAME_OOT, "Soul of Skulltulas", "fleet.soul.skulltula" },
    { GAME_MM, "Soul of Skulltulas", "fleet.soul.skulltula" },
    { GAME_OOT, "Soul of Stalchildren", "fleet.soul.stalchild" },
    { GAME_MM, "Soul of Stalchildren", "fleet.soul.stalchild" },
    { GAME_OOT, "Soul of Tektites", "fleet.soul.tektite" },
    { GAME_MM, "Soul of Tektites", "fleet.soul.tektite" },
    { GAME_OOT, "Soul of Wallmasters", "fleet.soul.wallmaster" },
    { GAME_MM, "Soul of Wallmasters", "fleet.soul.wallmaster" },
    { GAME_OOT, "Soul of Wolfos", "fleet.soul.wolfos" },
    { GAME_MM, "Soul of Wolfos", "fleet.soul.wolfos" },
};

struct SameItemAlias {
    GameId game;
    const char* anchor;
    const char* alias;
};

inline constexpr SameItemAlias kSameItemAliases[] = {
    { GAME_MM, "Pendant of Memories", "nei.equip.pendant" },
    { GAME_OOT, "Cojiro", "nei.equip.cojiro" },
};

struct TierAlias {
    GameId game;
    const char* anchor;
    const char* alias;
};

inline constexpr TierAlias kTierAliases[] = {
    { GAME_OOT, "Biggoron's Sword", "Progressive Goron Sword" },
    { GAME_MM, "nei.progressive.bgs", "Great Fairy's Sword" },
};

inline bool IsTierAliasName(GameId game, const std::string& name) {
    return std::any_of(std::begin(kTierAliases), std::end(kTierAliases), [&](const TierAlias& tier) {
        return tier.game == game && (name == tier.anchor || name == tier.alias);
    });
}

inline std::string FindNativePairKey(GameId game, const std::string& name) {
    for (const NativePairKey& entry : kNativePairKeys) {
        if (entry.game == game && name == entry.name) {
            return entry.key;
        }
    }
    return "";
}

inline bool IsUnclaimedHolderKey(const std::string& key) {
    if (key.rfind("nei.combo.", 0) != 0) {
        return false;
    }
    return std::none_of(std::begin(kNativePairKeys), std::end(kNativePairKeys),
                        [&](const NativePairKey& entry) { return key == entry.key; });
}

inline std::vector<CwSharedPair> ResolveModPairs(const std::string& ootDump, const std::string& mmDump) {
    std::vector<CwSharedPair> pairs;
    std::map<std::string, std::string> names[2];
    const std::string dumps[] = { ootDump, mmDump };
    for (int game = 0; game < 2; game++) {
        const auto dump = nlohmann::json::parse(dumps[game], nullptr, false);
        if (!dump.is_object()) {
            continue;
        }
        for (const auto& item : dump.value("items", nlohmann::json::array())) {
            const std::string name = item.value("name", "");
            const std::string tableKey = FindNativePairKey(game == 0 ? GAME_OOT : GAME_MM, name);
            const bool isNative = item.contains("pairKey") || !tableKey.empty();
            const std::string key = item.value("pairKey", item.value("textKey", tableKey));
            if (key.empty() || name.empty() || IsUnclaimedHolderKey(key)) {
                continue;
            }
            if (isNative) {
                names[game][key] = name;
            } else {
                names[game].emplace(key, name);
            }
        }
    }
    for (const auto& [key, name] : names[0]) {
        const auto peer = names[1].find(key);
        if (peer != names[1].end()) {
            pairs.push_back({ name, peer->second, key });
        }
    }
    return pairs;
}

inline nlohmann::json EncodeModPairs(const std::vector<CwSharedPair>& pairs) {
    auto result = nlohmann::json::array();
    for (const auto& pair : pairs) {
        if (!pair.key.empty()) {
            result.push_back({ { "key", pair.key }, { "oot", pair.ootName }, { "mm", pair.mmName } });
        }
    }
    return result;
}

inline bool IsSharedItem(GameId game, const std::string& name, uint64_t mask, const std::vector<CwSharedPair>& pairs) {
    for (const auto& p : pairs)
        if (name == (game == GAME_OOT ? p.ootName : p.mmName))
            return true;
    for (int i = 0; i < SF_COUNT; ++i) {
        const auto& def = SharedFamilyByIndex(i);
        if ((mask & (uint64_t{ 1 } << i)) &&
            ((game == GAME_OOT && name == def.ootName) || (game == GAME_MM && def.mmHasItem && name == def.mmName)))
            return true;
    }
    return false;
}

inline nlohmann::json BuildForeignArray(const nlohmann::json& foreignArray, uint64_t sharedMask = 0,
                                        const std::vector<CwSharedPair>& sharedPairs = {}) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& fm : foreignArray) {
        std::string checkGame = fm.value("checkGame", "");
        std::string checkName = fm.value("checkName", "");
        if (checkGame.empty() || checkName.empty())
            continue;
        std::string itemGame = fm.value("itemGame", "");
        std::string itemName = fm.value("itemName", "");
        const bool shared = IsSharedItem(KeyToGameId(itemGame), itemName, sharedMask, sharedPairs);
        const bool tagged = !shared && (itemGame == "mm" || itemGame == "oot");
        const char* suffix = GameSuffix(KeyToGameId(itemGame));
        auto tag = [&](std::string s) {
            s = StripGameSuffix(std::move(s));
            if (!s.empty() && tagged)
                s += suffix;
            return s;
        };
        std::string displayName = tag(fm.value("displayName", itemName));
        nlohmann::json entry = { { "checkGame", checkGame },
                                 { "checkName", checkName },
                                 { "itemGame", itemGame },
                                 { "itemName", itemName },
                                 { "displayName", displayName } };
        if (shared)
            entry["shared"] = true;
        if (fm.value("advancement", false))
            entry["advancement"] = true;
        if (fm.value("trap", false))
            entry["trap"] = true;
        std::string category = fm.value("category", "");
        if (!category.empty())
            entry["category"] = category;
        // fakeItemName stays bare because it is a grant-namespace key.
        std::string fakeItemName = fm.value("fakeItemName", "");
        if (!fakeItemName.empty()) {
            entry["fakeItemName"] = fakeItemName;
            entry["fakeDisplayName"] = tag(fm.value("fakeDisplayName", fakeItemName));
            entry["fakeTrickName"] = tag(fm.value("fakeTrickName", fm.value("fakeDisplayName", fakeItemName)));
        }
        out.push_back(std::move(entry));
    }
    return out;
}

inline std::string EncodeUnsharedSpec(const std::vector<std::string>& unsharedKeys) {
    if (unsharedKeys.empty()) {
        return "";
    }
    nlohmann::json spec = nlohmann::json::object();
    spec["unshared"] = unsharedKeys;
    return spec.dump();
}

inline void DropUnsharedPairs(std::vector<CwSharedPair>& pairs, const nlohmann::json& spec) {
    if (!spec.is_object()) {
        return;
    }
    std::set<std::string> unshared;
    for (const auto& key : spec.value("unshared", nlohmann::json::array())) {
        if (key.is_string()) {
            unshared.insert(key.get<std::string>());
        }
    }
    std::erase_if(pairs, [&unshared](const CwSharedPair& pair) { return unshared.contains(pair.key); });
}

inline std::unordered_map<std::string, std::string> ReadMmItemTokens(const std::string& mmDumpJson) {
    std::unordered_map<std::string, std::string> tokenToName;
    const auto dump = nlohmann::json::parse(mmDumpJson, nullptr, false);
    if (!dump.is_object()) {
        return tokenToName;
    }
    for (const auto& item : dump.value("items", nlohmann::json::array())) {
        std::string token = item.value("token", std::string{});
        std::string name = item.value("name", std::string{});
        if (!token.empty() && !name.empty()) {
            tokenToName.emplace(std::move(token), std::move(name));
        }
    }
    return tokenToName;
}

// The OoT dump speaks RI_* tokens while MM's dump uses display names, so joining needs the token->name map.
inline void AppendTokenPairs(std::vector<CwSharedPair>& pairs, const nlohmann::json& tokenPairs,
                             const std::string& mmDumpJson) {
    const auto tokenToName = ReadMmItemTokens(mmDumpJson);
    if (tokenToName.empty()) {
        std::cerr << "[ComboShip] shared items: MM dump carries no item tokens; RI_*-keyed pairs cannot "
                     "match - rebuild 2ship.dll\n";
    }
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto& pair : tokenPairs) {
        std::string oot = pair.value("oot", "");
        std::string mm = pair.value("mm", "");
        if (oot.empty() || mm.empty()) {
            continue;
        }
        const auto named = tokenToName.find(mm);
        if (named != tokenToName.end()) {
            mm = named->second;
        } else if (mm.rfind("RI_", 0) == 0) {
            std::cerr << "[ComboShip] shared items: unresolved MM token '" << mm << "', pair skipped\n";
            continue;
        }
        if (seen.emplace(oot, mm).second) {
            pairs.push_back({ std::move(oot), std::move(mm) });
        }
    }
}

inline std::vector<CwSharedPair> ResolveSharedPairs(const std::string& sharedPairsJson, const std::string& mmDumpJson,
                                                    const std::string& ootDumpJson = "") {
    std::vector<CwSharedPair> pairs = ResolveModPairs(ootDumpJson, mmDumpJson);
    if (sharedPairsJson.empty()) {
        return pairs;
    }
    try {
        const auto spec = nlohmann::json::parse(sharedPairsJson);
        DropUnsharedPairs(pairs, spec);
        const auto tokenPairs = spec.is_object() ? spec.value("pairs", nlohmann::json::array()) : spec;
        if (!tokenPairs.empty()) {
            AppendTokenPairs(pairs, tokenPairs, mmDumpJson);
        }
    } catch (const std::exception& e) {
        std::cerr << "[ComboShip] shared items: pairs parse error, sharing off: " << e.what() << "\n";
        pairs.clear();
    }
    return pairs;
}

inline std::set<std::string> SharedPairNames(const std::vector<CwSharedPair>& pairs) {
    std::set<std::string> names;
    for (const auto& p : pairs) {
        names.insert(p.ootName);
        names.insert(p.mmName);
    }
    return names;
}

struct BottleTwin {
    const char* oot;
    const char* mm;
    bool isCritical = false;
};

inline constexpr BottleTwin kBottleTwins[] = {
    { "Empty Bottle", "Empty Bottle" },
    { "Bottle with Milk", "Bottle of Milk" },
    { "Bottle with Red Potion", "Bottle with Red Potion" },
    { "Bottle with Green Potion", "fleet.bottle.green_potion" },
    { "Bottle with Blue Potion", "fleet.bottle.blue_potion" },
    { "Bottle with Fairy", "fleet.bottle.fairy" },
    { "Bottle with Fish", "fleet.bottle.fish" },
    { "Bottle with Bugs", "fleet.bottle.bugs" },
    { "Bottle with Poe", "fleet.bottle.poe" },
    { "Bottle with Big Poe", "fleet.bottle.big_poe" },
    { "Bottle with Blue Fire", "fleet.bottle.blue_fire" },
    { "Bottle with Ruto's Letter", "fleet.bottle.ruto_letter", true },
    { "fleet.bottle.gold_dust", "Bottle With Gold Dust", true },
    { "fleet.bottle.chateau", "Bottle of Chateau Romani" },
};

inline constexpr const char* kBottlePairKeys[] = { "fleet.bottle.empty", "fleet.bottle.milk",
                                                   "fleet.bottle.red_potion" };

inline const BottleTwin* FindBottleTwin(GameId game, const std::string& name) {
    const auto found = std::find_if(std::begin(kBottleTwins), std::end(kBottleTwins), [&](const BottleTwin& twin) {
        return name == (game == GAME_OOT ? twin.oot : twin.mm);
    });
    return found == std::end(kBottleTwins) ? nullptr : found;
}

inline std::set<std::string> SharedUntaggedNames(uint64_t mask, const std::vector<CwSharedPair>& pairs) {
    auto names = SharedUntaggedNames(mask);
    auto nei = SharedPairNames(pairs);
    names.insert(nei.begin(), nei.end());
    if ((mask & (uint64_t{ 1 } << SF_BOTTLES)) != 0) {
        for (const BottleTwin& twin : kBottleTwins) {
            names.insert(twin.oot);
            names.insert(twin.mm);
        }
    }
    return names;
}

struct CwSharedName {
    GameId game;
    std::string name;
};

struct CwLadderStep {
    const char* name;
    size_t slots;
};

struct CwSharedGroup {
    std::vector<CwSharedName> names;
    bool nei = false;
    size_t mmTierCap = std::numeric_limits<size_t>::max();
    bool nativeFamily = false;
    bool countsMmTiers = false;
    std::array<std::vector<CwLadderStep>, 2> ladders;
};

struct TierLadder {
    int family;
    std::vector<CwLadderStep> oot;
    std::vector<CwLadderStep> mm;
};

inline const std::vector<TierLadder>& TierLadders() {
    static const std::vector<TierLadder> ladders = {
        { SF_WALLET,
          { { "Progressive Wallet", 5 } },
          { { "nei.bag.child_wallet", 1 }, { "Progressive Wallet", 3 }, { "nei.infinite.wallet", 1 } } },
        { SF_BOW, { { "Progressive Bow", 4 } }, { { "Progressive Bow", 3 }, { "nei.infinite.quiver", 1 } } },
        { SF_BOMB_BAG,
          { { "Progressive Bomb Bag", 4 } },
          { { "Progressive Bomb Bag", 3 }, { "nei.infinite.bomb_bag", 1 } } },
        { SF_MAGIC, { { "Progressive Magic Meter", 3 } }, { { "Progressive Magic", 2 }, { "nei.infinite.magic", 1 } } },
        { SF_BOMBCHU_BAG,
          { { "Bombchu Bag", 4 } },
          { { "nei.bag.bombchus", 1 }, { "", 2 }, { "nei.infinite.bombchus", 1 } } },
        { SF_DEKU_STICK_BAG,
          { { "Progressive Stick Capacity", 4 } },
          { { "nei.bag.deku_sticks", 1 }, { "", 2 }, { "nei.infinite.deku_sticks", 1 } } },
        { SF_DEKU_NUT_BAG,
          { { "Progressive Nut Capacity", 4 } },
          { { "nei.bag.deku_nuts", 1 }, { "", 2 }, { "nei.infinite.deku_nuts", 1 } } },
    };
    return ladders;
}

inline const TierLadder* FindTierLadder(int family) {
    const auto& ladders = TierLadders();
    const auto found =
        std::find_if(ladders.begin(), ladders.end(), [&](const TierLadder& ladder) { return ladder.family == family; });
    return found == ladders.end() ? nullptr : &*found;
}

// Edges meeting at the same MM item must be joined first, or that pickup is counted twice.
inline std::vector<CwSharedGroup> BuildSharedItemGroups(const std::vector<CwSharedPair>& pairs, uint64_t mask) {
    std::vector<CwSharedGroup> groups;
    auto connect = [&](const std::string& oot, const std::string& mm, bool nei, size_t cap) {
        size_t oi = groups.size(), mi = groups.size();
        for (size_t i = 0; i < groups.size(); ++i)
            for (const auto& n : groups[i].names) {
                if (n.game == GAME_OOT && n.name == oot)
                    oi = i;
                if (n.game == GAME_MM && n.name == mm)
                    mi = i;
            }
        const size_t missing = groups.size();
        if (oi == missing && mi == missing) {
            groups.push_back({ { { GAME_OOT, oot }, { GAME_MM, mm } }, nei, cap, !nei });
            return;
        }
        size_t at = oi != missing ? oi : mi;
        if (oi == missing)
            groups[at].names.push_back({ GAME_OOT, oot });
        if (mi == missing)
            groups[at].names.push_back({ GAME_MM, mm });
        if (oi != missing && mi != missing && oi != mi) {
            groups[oi].names.insert(groups[oi].names.end(), groups[mi].names.begin(), groups[mi].names.end());
            groups[oi].nei = groups[oi].nei || groups[mi].nei;
            groups[oi].nativeFamily = groups[oi].nativeFamily || groups[mi].nativeFamily;
            groups[oi].mmTierCap = std::max(groups[oi].mmTierCap, groups[mi].mmTierCap);
            groups[oi].countsMmTiers = groups[oi].countsMmTiers || groups[mi].countsMmTiers;
            groups.erase(groups.begin() + mi);
            at = oi - (mi < oi ? 1 : 0);
        }
        groups[at].nei = groups[at].nei || nei;
        groups[at].nativeFamily = groups[at].nativeFamily || !nei;
        groups[at].mmTierCap = std::max(groups[at].mmTierCap, cap);
    };
    for (const auto& p : pairs)
        connect(p.ootName, p.mmName, true, std::numeric_limits<size_t>::max());
    for (int i = 0; i < SF_COUNT; ++i) {
        const auto& def = SharedFamilyByIndex(i);
        if (!(mask & (uint64_t{ 1 } << i)) || !def.mmHasItem)
            continue;
        connect(def.ootName, def.mmName, false, static_cast<size_t>(def.mmTierCap));
        for (auto& group : groups) {
            auto n = std::find_if(group.names.begin(), group.names.end(),
                                  [&](const CwSharedName& n) { return n.game == GAME_OOT && n.name == def.ootName; });
            if (n != group.names.end()) {
                std::rotate(group.names.begin(), n, n + 1);
                group.countsMmTiers = group.countsMmTiers || CountsMmTiers(def);
            }
        }
    }
    const auto findPair = [&](const std::string& key) {
        return std::find_if(pairs.begin(), pairs.end(), [&](const CwSharedPair& pair) { return pair.key == key; });
    };
    for (const FoldedPair& folded : kFoldedPairs) {
        const auto pair = findPair(folded.key);
        if (pair == pairs.end()) {
            continue;
        }
        if (folded.hostFamily != kNoSharedFamily) {
            const auto& host = SharedFamilyByIndex(folded.hostFamily);
            if ((mask & (uint64_t{ 1 } << folded.hostFamily)) != 0) {
                connect(host.ootName, pair->mmName, true, std::numeric_limits<size_t>::max());
            }
        } else if (const auto host = findPair(folded.hostPair); host != pairs.end()) {
            connect(host->ootName, pair->mmName, true, std::numeric_limits<size_t>::max());
        }
    }
    if ((mask & (uint64_t{ 1 } << SF_BOTTLES)) != 0) {
        for (const BottleTwin& twin : kBottleTwins) {
            connect(twin.oot, twin.mm, true, std::numeric_limits<size_t>::max());
        }
    }
    for (const TierLadder& ladder : TierLadders()) {
        if ((mask & (uint64_t{ 1 } << ladder.family)) == 0) {
            continue;
        }
        const char* ootName = SharedFamilyByIndex(ladder.family).ootName;
        for (const CwLadderStep& step : ladder.mm) {
            if (*step.name != '\0') {
                connect(ootName, step.name, true, std::numeric_limits<size_t>::max());
            }
        }
        for (auto& group : groups) {
            const bool holdsChain = std::any_of(group.names.begin(), group.names.end(), [&](const CwSharedName& n) {
                return n.game == GAME_OOT && n.name == ootName;
            });
            if (holdsChain) {
                group.ladders = { ladder.oot, ladder.mm };
            }
        }
    }
    for (const SameItemAlias& same : kSameItemAliases) {
        for (auto& group : groups) {
            const auto anchor = std::find_if(group.names.begin(), group.names.end(), [&](const CwSharedName& n) {
                return n.game == same.game && n.name == same.anchor;
            });
            if (anchor != group.names.end()) {
                group.names.insert(anchor + 1, { same.game, same.alias });
                break;
            }
        }
    }
    for (const TierAlias& tier : kTierAliases) {
        for (auto& group : groups) {
            const auto anchor = std::find_if(group.names.begin(), group.names.end(), [&](const CwSharedName& n) {
                return n.game == tier.game && n.name == tier.anchor;
            });
            if (anchor != group.names.end()) {
                group.names.insert(anchor + 1, { tier.game, tier.alias });
                break;
            }
        }
    }
    return groups;
}

// Mirrored aliases must never flow back into the pickup vectors, or progressive tiers inflate on the next sphere.
inline void SharedOwnedViews(const std::vector<CwSharedGroup>& groups, const std::vector<std::string>& ootOwned,
                             const std::vector<std::string>& mmOwned, std::vector<std::string>& ootSeen,
                             std::vector<std::string>& mmSeen) {
    ootSeen = ootOwned;
    mmSeen = mmOwned;
    if (groups.empty())
        return;
    std::unordered_map<std::string, size_t> counts[2];
    for (const auto& n : ootOwned)
        ++counts[GAME_OOT][n];
    for (const auto& n : mmOwned)
        ++counts[GAME_MM][n];
    for (const auto& group : groups) {
        size_t copies = 0;
        for (const auto& n : group.names) {
            copies += counts[n.game][n.name];
        }
        if (!group.ladders[GAME_OOT].empty()) {
            for (GameId game : { GAME_OOT, GAME_MM }) {
                auto& seen = game == GAME_OOT ? ootSeen : mmSeen;
                size_t levels = copies;
                for (const CwLadderStep& step : group.ladders[game]) {
                    const size_t taken = std::min(levels, step.slots);
                    levels -= taken;
                    const size_t native = *step.name == '\0' ? taken : counts[game][step.name];
                    if (taken > native) {
                        seen.insert(seen.end(), taken - native, step.name);
                    }
                }
            }
            continue;
        }
        for (const auto& n : group.names) {
            auto& seen = n.game == GAME_OOT ? ootSeen : mmSeen;
            const size_t native = counts[n.game][n.name];
            const size_t target = n.game == GAME_MM && !group.nei ? std::min(copies, group.mmTierCap) : copies;
            if (target > native)
                seen.insert(seen.end(), target - native, n.name);
        }
    }
}

// Only native placements are suffixed; foreign checks and sharedNames are skipped.
inline void SuffixCrossGameItems(nlohmann::json& ootPlacements, nlohmann::json& mmPlacements,
                                 const nlohmann::json& foreignArray, const std::string& sohDump,
                                 const std::string& mmDump, const std::set<std::string>& sharedNames = {}) {
    auto itemNames = [](const std::string& dump) {
        std::set<std::string> s;
        try {
            auto d = nlohmann::json::parse(dump);
            for (auto& it : d.value("pool", nlohmann::json::array()))
                if (auto n = it.value("name", std::string{}); !n.empty())
                    s.insert(std::move(n));
            for (auto& it : d.value("items", nlohmann::json::array()))
                if (auto n = it.value("name", std::string{}); !n.empty())
                    s.insert(std::move(n));
            for (auto& f : d.value("fixed", nlohmann::json::array()))
                if (auto n = f.value("item", std::string{}); !n.empty())
                    s.insert(std::move(n));
        } catch (...) {}
        return s;
    };
    std::set<std::string> ootSet = itemNames(sohDump), mmSet = itemNames(mmDump), shared;
    for (const auto& n : ootSet)
        if (mmSet.count(n) && !sharedNames.count(n))
            shared.insert(n);
    if (shared.empty())
        return;
    std::set<std::string> ootForeign, mmForeign;
    for (const auto& fm : foreignArray) {
        std::string cg = fm.value("checkGame", ""), cn = fm.value("checkName", "");
        if (cn.empty())
            continue;
        (cg == "oot" ? ootForeign : mmForeign).insert(cn);
    }
    auto process = [&](nlohmann::json& pl, const std::set<std::string>& foreign, const char* suf) {
        for (auto it = pl.begin(); it != pl.end(); ++it) {
            if (!it.value().is_string() || foreign.count(it.key()))
                continue;
            std::string v = it.value().get<std::string>();
            if (shared.count(v))
                it.value() = v + suf;
        }
    };
    process(ootPlacements, ootForeign, " (OOT)");
    process(mmPlacements, mmForeign, " (MM)");
}

// Returns empty on missing or corrupt data; never throws across the channel. `slot` is unused.
inline std::unordered_map<std::string, ForeignItem> LoadForeignForGame(int slot, GameId checkGame) {
    (void)slot;
    std::unordered_map<std::string, ForeignItem> map;
    if (g_comboForeignJson.empty())
        return map;
    try {
        nlohmann::json j = nlohmann::json::parse(g_comboForeignJson);
        const std::string key = GameIdToKey(checkGame);
        for (const auto& fm : j.value("foreign", nlohmann::json::array())) {
            if (fm.value("checkGame", "") != key)
                continue;
            ForeignItem fi;
            fi.itemGame = KeyToGameId(fm.value("itemGame", ""));
            fi.itemName = fm.value("itemName", "");
            fi.displayName = fm.value("displayName", fi.itemName);
            fi.advancement = fm.value("advancement", false);
            fi.trap = fm.value("trap", false);
            fi.shared = fm.value("shared", false);
            fi.category = fm.value("category", "");
            fi.fakeItemName = fm.value("fakeItemName", "");
            fi.fakeDisplayName = fm.value("fakeDisplayName", "");
            fi.fakeTrickName = fm.value("fakeTrickName", "");
            map.emplace(fm.value("checkName", ""), std::move(fi));
        }
    } catch (...) {}
    return map;
}

struct ForeignPlacement {
    GameId checkGame;
    std::string checkName;
    std::string displayName;
    bool advancement = false;
};

inline std::unordered_map<std::string, ForeignPlacement> LoadForeignByItem(int slot, GameId itemGame) {
    (void)slot;
    std::unordered_map<std::string, ForeignPlacement> map;
    if (g_comboForeignJson.empty())
        return map;
    try {
        nlohmann::json j = nlohmann::json::parse(g_comboForeignJson);
        const std::string key = GameIdToKey(itemGame);
        for (const auto& fm : j.value("foreign", nlohmann::json::array())) {
            if (fm.value("itemGame", "") != key)
                continue;
            ForeignPlacement fp;
            fp.checkGame = KeyToGameId(fm.value("checkGame", ""));
            fp.checkName = fm.value("checkName", "");
            fp.displayName = fm.value("displayName", fp.checkName);
            fp.advancement = fm.value("advancement", false);
            map.emplace(fm.value("itemName", ""), std::move(fp));
        }
    } catch (...) {}
    return map;
}

struct HintGossipEntry {
    uint32_t weight = 1;
    std::string text;
};
struct MmHints {
    std::vector<HintGossipEntry> gossipPool;
    std::unordered_map<std::string, std::string> itemLocations;
};

inline MmHints LoadHintsMM(int slot) {
    (void)slot;
    MmHints out;
    if (g_comboForeignJson.empty())
        return out;
    try {
        nlohmann::json j = nlohmann::json::parse(g_comboForeignJson);
        auto mm = j.value("hints", nlohmann::json::object()).value("mm", nlohmann::json::object());
        for (auto& g : mm.value("gossipPool", nlohmann::json::array()))
            out.gossipPool.push_back({ g.value("weight", 1u), g.value("text", "") });
        const auto itemLocs = mm.value("itemLocations", nlohmann::json::object());
        for (auto& [k, v] : itemLocs.items())
            out.itemLocations.emplace(k, v.get<std::string>());
    } catch (...) {}
    return out;
}

} // namespace ComboRando
