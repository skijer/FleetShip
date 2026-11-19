#pragma once

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ComboRando {

enum SharedFamily {
    SF_BOW = 0,
    SF_BOMB_BAG,
    SF_BOMBCHU_BAG,
    SF_MAGIC,
    SF_WALLET,
    SF_HOOKSHOT,
    SF_FIRE_ARROWS,
    SF_ICE_ARROWS,
    SF_LIGHT_ARROWS,
    SF_LENS,
    SF_EPONAS_SONG,
    SF_SONG_OF_STORMS,
    SF_GORON_MASK,
    SF_ZORA_MASK,
    SF_KEATON_MASK,
    SF_BUNNY_HOOD,
    SF_MASK_OF_TRUTH,
    SF_SONG_OF_TIME,
    SF_SUNS_SONG,
    SF_SARIAS_SONG,
    SF_OCARINA,
    SF_HEART_PIECES,
    SF_HEART_CONTAINERS,
    SF_DOUBLE_DEFENSE,
    SF_HERO_SHIELD,
    SF_SKELETON_KEY,
    SF_OCARINA_A,
    SF_OCARINA_C_UP,
    SF_OCARINA_C_DOWN,
    SF_OCARINA_C_LEFT,
    SF_OCARINA_C_RIGHT,
    SF_SCALE,
    SF_CLIMB,
    SF_CRAWL,
    SF_OPEN_CHESTS,
    SF_SPEAK_DEKU,
    SF_SPEAK_GERUDO,
    SF_SPEAK_GORON,
    SF_SPEAK_HYLIAN,
    SF_SPEAK_KOKIRI,
    SF_SPEAK_ZORA,
    SF_STONE_OF_AGONY,
    SF_GERUDO_CARD,
    SF_BLUE_FIRE,
    SF_GREAT_SPIN,
    SF_FISHING_POLE,
    SF_STRENGTH,
    SF_GRAB,
    SF_DEKU_STICK_BAG,
    SF_DEKU_NUT_BAG,
    SF_CHILD_WALLET,
    SF_GOLDEN_HEARTS,
    SF_SWORD,
    SF_BOTTLES,
    SF_LIVE_RUPEES,
    SF_LIVE_HEALTH,
    SF_LIVE_MAGIC,
    SF_LIVE_EQUIPS,
    SF_GOLDEN_HEART_PIECES,
    SF_COUNT
};

static_assert(SF_COUNT <= 64);

constexpr int kSharedCopiesCap = 255;

struct SharedFamilyDef {
    SharedFamily family;
    const char* key;
    const char* cvar;
    const char* label;
    const char* tooltip;
    const char* ootName;
    const char* mmName;
    int mmTierCap;
    bool isMask;
    bool mmHasItem;
    bool ootToMmOnly;
};

inline const SharedFamilyDef* SharedFamilyTable() {
    static const SharedFamilyDef table[SF_COUNT] = {
        { SF_BOW, "bows", "gCombo.Rando.Shared.Bows", "Shared Bows", "One Progressive Bow counts for both games.",
          "Progressive Bow", "Progressive Bow", 3, false, true, false },
        { SF_BOMB_BAG, "bombBags", "gCombo.Rando.Shared.BombBags", "Shared Bomb Bags",
          "One Progressive Bomb Bag counts for both games.", "Progressive Bomb Bag", "Progressive Bomb Bag", 3, false,
          true, false },
        { SF_BOMBCHU_BAG, "bombchuBags", "gCombo.Rando.Shared.BombchuBags", "Shared Bombchu Bags",
          "One Bombchu Bag counts for both games. Requires OOT Bombchu Bag != None.", "Bombchu Bag", "nei.bag.bombchus",
          1, false, true, false },
        { SF_MAGIC, "magic", "gCombo.Rando.Shared.Magic", "Shared Magic",
          "One Progressive Magic Meter counts for both games.", "Progressive Magic Meter", "Progressive Magic", 2,
          false, true, false },
        { SF_WALLET, "wallets", "gCombo.Rando.Shared.Wallets", "Shared Wallets",
          "One Progressive Wallet counts for both games, Child and Infinite Wallet included.", "Progressive Wallet",
          "Progressive Wallet", 3, false, true, false },
        { SF_HOOKSHOT, "hookshot", "gCombo.Rando.Shared.Hookshot", "Shared Hookshot",
          "One Progressive Hookshot counts for both games (MM only needs tier 1).", "Progressive Hookshot", "Hookshot",
          1, false, true, false },
        { SF_FIRE_ARROWS, "fireArrows", "gCombo.Rando.Shared.FireArrows", "Shared Fire Arrows",
          "One Fire Arrows counts for both games.", "Fire Arrows", "Fire Arrows", 1, false, true, false },
        { SF_ICE_ARROWS, "iceArrows", "gCombo.Rando.Shared.IceArrows", "Shared Ice Arrows",
          "One Ice Arrows counts for both games.", "Ice Arrows", "Ice Arrows", 1, false, true, false },
        { SF_LIGHT_ARROWS, "lightArrows", "gCombo.Rando.Shared.LightArrows", "Shared Light Arrows",
          "One Light Arrows counts for both games.", "Light Arrows", "Light Arrows", 1, false, true, false },
        { SF_LENS, "lens", "gCombo.Rando.Shared.Lens", "Shared Lens of Truth",
          "One Lens of Truth counts for both games.", "Lens of Truth", "Lens of Truth", 1, false, true, false },
        { SF_EPONAS_SONG, "eponasSong", "gCombo.Rando.Shared.EponasSong", "Shared Epona's Song",
          "One Epona's Song counts for both games.", "Epona's Song", "Epona's Song", 1, false, true, false },
        { SF_SONG_OF_STORMS, "songOfStorms", "gCombo.Rando.Shared.SongOfStorms", "Shared Song of Storms",
          "One Song of Storms counts for both games.", "Song of Storms", "Song of Storms", 1, false, true, false },
        { SF_GORON_MASK, "goronMask", "gCombo.Rando.Shared.GoronMask", "Shared Goron Mask",
          "One Goron Mask counts for both games. Requires OOT Shuffle Masks.", "Goron Mask", "Goron Mask", 1, true,
          true, false },
        { SF_ZORA_MASK, "zoraMask", "gCombo.Rando.Shared.ZoraMask", "Shared Zora Mask",
          "One Zora Mask counts for both games. Requires OOT Shuffle Masks.", "Zora Mask", "Zora Mask", 1, true, true,
          false },
        { SF_KEATON_MASK, "keatonMask", "gCombo.Rando.Shared.KeatonMask", "Shared Keaton Mask",
          "One Keaton Mask counts for both games. Requires OOT Shuffle Masks.", "Keaton Mask", "Keaton Mask", 1, true,
          true, false },
        { SF_BUNNY_HOOD, "bunnyHood", "gCombo.Rando.Shared.BunnyHood", "Shared Bunny Hood",
          "One Bunny Hood counts for both games. Requires OOT Shuffle Masks.", "Bunny Hood", "Bunny Hood", 1, true,
          true, false },
        { SF_MASK_OF_TRUTH, "maskOfTruth", "gCombo.Rando.Shared.MaskOfTruth", "Shared Mask of Truth",
          "One Mask of Truth counts for both games. Requires OOT Shuffle Masks.", "Mask of Truth", "Mask of Truth", 1,
          true, true, false },
        { SF_SONG_OF_TIME, "songOfTime", "gCombo.Rando.Shared.SongOfTime", "Shared Song of Time",
          "One Song of Time counts for both games.", "Song of Time", "Song of Time", 1, false, true, false },
        { SF_SUNS_SONG, "sunsSong", "gCombo.Rando.Shared.SunsSong", "Shared Sun's Song",
          "One Sun's Song counts for both games.", "Sun's Song", "Sun's Song", 1, false, true, false },
        { SF_SARIAS_SONG, "sariasSong", "gCombo.Rando.Shared.SariasSong", "Shared Saria's Song",
          "One Saria's Song counts for both games.", "Saria's Song", "Saria's Song", 1, false, true, false },
        { SF_OCARINA, "ocarina", "gCombo.Rando.Shared.Ocarina", "Shared Ocarinas",
          "Two Progressive Ocarinas count for both games; MM's Ocarina of Time comes with the first.",
          "Progressive Ocarina", "Ocarina of Time", 1, false, true, false },
        { SF_HEART_PIECES, "health", "gCombo.Rando.Shared.Health", "Shared Health",
          "Heart Pieces, Heart Containers and Double Defense count for both games.", "Piece of Heart", "Heart Piece",
          kSharedCopiesCap, false, true, false },
        { SF_HEART_CONTAINERS, "health", "gCombo.Rando.Shared.Health", "Shared Health",
          "Heart Pieces, Heart Containers and Double Defense count for both games.", "Heart Container",
          "Heart Container", kSharedCopiesCap, false, true, false },
        { SF_DOUBLE_DEFENSE, "health", "gCombo.Rando.Shared.Health", "Shared Health",
          "Heart Pieces, Heart Containers and Double Defense count for both games.", "Double Defense", "Double Defense",
          1, false, true, false },
        { SF_HERO_SHIELD, "shields", "gCombo.Rando.Shared.Shields", "Shared Shields",
          "The Hylian Shield is MM's Hero's Shield.", "Hylian Shield", "Hero's Shield", 1, false, true, false },
        { SF_SKELETON_KEY, "skeletonKey", "gCombo.Rando.Shared.SkeletonKey", "Shared Skeleton Key",
          "One Skeleton Key opens every locked door in both games.", "Skeleton Key", "Skeleton Key", 1, false, true,
          false },
        { SF_OCARINA_A, "ocarinaButtons", "gCombo.Rando.Shared.OcarinaButtons", "Shared Ocarina Buttons",
          "Each ocarina button found counts for both games.", "Ocarina A Button", "A Button", 1, false, true, false },
        { SF_OCARINA_C_UP, "ocarinaButtons", "gCombo.Rando.Shared.OcarinaButtons", "Shared Ocarina Buttons",
          "Each ocarina button found counts for both games.", "Ocarina C Up Button", "C Up Button", 1, false, true,
          false },
        { SF_OCARINA_C_DOWN, "ocarinaButtons", "gCombo.Rando.Shared.OcarinaButtons", "Shared Ocarina Buttons",
          "Each ocarina button found counts for both games.", "Ocarina C Down Button", "C Down Button", 1, false, true,
          false },
        { SF_OCARINA_C_LEFT, "ocarinaButtons", "gCombo.Rando.Shared.OcarinaButtons", "Shared Ocarina Buttons",
          "Each ocarina button found counts for both games.", "Ocarina C Left Button", "C Left Button", 1, false, true,
          false },
        { SF_OCARINA_C_RIGHT, "ocarinaButtons", "gCombo.Rando.Shared.OcarinaButtons", "Shared Ocarina Buttons",
          "Each ocarina button found counts for both games.", "Ocarina C Right Button", "C Right Button", 1, false,
          true, false },
        { SF_SCALE, "scale", "gCombo.Rando.Shared.SCALE", "Shared Swim",
          "OoT's first Progressive Scale is MM's Ability to Swim.", "Progressive Scale", "Ability to Swim", 1, false,
          true, false },
        { SF_CLIMB, "climb", "gCombo.Rando.Shared.CLIMB", "Shared Climb",
          "One reward counts for both games when both pools supply it.", "Climb", "nei.skill.climb", 1, false, true,
          false },
        { SF_CRAWL, "crawl", "gCombo.Rando.Shared.CRAWL", "Shared Crawl",
          "One reward counts for both games when both pools supply it.", "Crawl", "nei.skill.crawl", 1, false, true,
          false },
        { SF_OPEN_CHESTS, "openChests", "gCombo.Rando.Shared.OPEN_CHESTS", "Shared Open Chests",
          "One reward counts for both games when both pools supply it.", "Open Chests", "nei.skill.open_chests", 2,
          false, true, false },
        { SF_SPEAK_DEKU, "speakDeku", "gCombo.Rando.Shared.SPEAK_DEKU", "Shared Speak Deku",
          "One reward counts for both games when both pools supply it.", "Deku Jabber Nut", "nei.skill.speak_deku", 1,
          false, true, false },
        { SF_SPEAK_GERUDO, "speakGerudo", "gCombo.Rando.Shared.SPEAK_GERUDO", "Shared Speak Gerudo",
          "One reward counts for both games when both pools supply it.", "Gerudo Jabber Nut", "nei.skill.speak_gerudo",
          1, false, true, false },
        { SF_SPEAK_GORON, "speakGoron", "gCombo.Rando.Shared.SPEAK_GORON", "Shared Speak Goron",
          "One reward counts for both games when both pools supply it.", "Goron Jabber Nut", "nei.skill.speak_goron", 1,
          false, true, false },
        { SF_SPEAK_HYLIAN, "speakHylian", "gCombo.Rando.Shared.SPEAK_HYLIAN", "Shared Speak Hylian",
          "One reward counts for both games when both pools supply it.", "Hylian Jabber Nut", "nei.skill.speak_hylian",
          1, false, true, false },
        { SF_SPEAK_KOKIRI, "speakKokiri", "gCombo.Rando.Shared.SPEAK_KOKIRI", "Shared Speak Kokiri",
          "One reward counts for both games when both pools supply it.", "Kokiri Jabber Nut", "nei.skill.speak_kokiri",
          1, false, true, false },
        { SF_SPEAK_ZORA, "speakZora", "gCombo.Rando.Shared.SPEAK_ZORA", "Shared Speak Zora",
          "One reward counts for both games when both pools supply it.", "Zora Jabber Nut", "nei.skill.speak_zora", 1,
          false, true, false },
        { SF_STONE_OF_AGONY, "stoneOfAgony", "gCombo.Rando.Shared.STONE_OF_AGONY", "Shared Stone of Agony",
          "One reward counts for both games when both pools supply it.", "Stone of Agony", "oot.stone_of_agony", 1,
          false, true, false },
        { SF_GERUDO_CARD, "gerudoCard", "gCombo.Rando.Shared.GERUDO_CARD", "Shared Gerudo Membership Card",
          "One reward counts for both games when both pools supply it.", "Gerudo Membership Card", "oot.gerudo_card", 1,
          false, true, false },
        { SF_BLUE_FIRE, "blueFire", "gCombo.Rando.Shared.BLUE_FIRE", "Shared Blue Fire / Hot Spring Water",
          "One reward counts for both games when both pools supply it.", "Bottle with Blue Fire",
          "nei.bottle.hot_spring_water", 1, false, true, false },
        { SF_GREAT_SPIN, "greatSpin", "gCombo.Rando.Shared.GREAT_SPIN", "Shared Great Spin Attack",
          "One reward counts for both games when both pools supply it.", "nei.skill.great_spin", "Great Spin Attack", 1,
          false, true, false },
        { SF_FISHING_POLE, "fishingPole", "gCombo.Rando.Shared.FISHING_POLE", "Shared Fishing Pole",
          "One reward counts for both games when both pools supply it.", "Fishing Pole", "nei.combo.fishing_pole", 1,
          false, true, false },
        // The chain's own pickup comes first: it is what an OoT Strength Upgrade becomes in MM.
        { SF_STRENGTH, "grab", "gCombo.Rando.Shared.GRAB", "Shared Strength",
          "Every Strength Upgrade found counts for both games; the first one lets Link grab.", "Strength Upgrade",
          "oot.progressive.strength", 4, false, true, false },
        { SF_GRAB, "grab", "gCombo.Rando.Shared.GRAB", "Shared Strength",
          "Every Strength Upgrade found counts for both games; the first one lets Link grab.", "Strength Upgrade",
          "oot.power_bracelet", 1, false, true, false },
        { SF_DEKU_STICK_BAG, "dekuStickBag", "gCombo.Rando.Shared.DEKU_STICK_BAG", "Shared Deku Stick Bag",
          "One Deku Stick Bag counts for both games.", "Progressive Stick Capacity", "nei.bag.deku_sticks", 1, false,
          true, false },
        { SF_DEKU_NUT_BAG, "dekuNutBag", "gCombo.Rando.Shared.DEKU_NUT_BAG", "Shared Deku Nut Bag",
          "One Deku Nut Bag counts for both games.", "Progressive Nut Capacity", "nei.bag.deku_nuts", 1, false, true,
          false },
        { SF_CHILD_WALLET, "childWallet", "gCombo.Rando.Shared.CHILD_WALLET", "Shared Child Wallet",
          "One Child Wallet counts for both games.", "Child Wallet", "nei.bag.child_wallet", 1, false, true, false },
        { SF_GOLDEN_HEARTS, "goldenHearts", "gCombo.Rando.Shared.GOLDEN_HEARTS", "Shared Golden Hearts",
          "Every Golden Heart Container and Piece found counts for both games.", "nei.heart.golden", "nei.heart.golden",
          kSharedCopiesCap, false, true, false },
        { SF_SWORD, "swords", "gCombo.Rando.Shared.SWORDS", "Shared Swords",
          "Kokiri, Razor and Gilded Sword: each sword tier found counts for both games.", "Kokiri Sword",
          "Progressive Sword", 3, false, true, false },
        { SF_BOTTLES, "bottles", "gCombo.Rando.Shared.BOTTLES", "Shared Bottles",
          "Every bottle found in one game also fills one in the other, with the same content where both games "
          "have it.",
          "Empty Bottle", "Empty Bottle", 8, false, false, false },
        { SF_LIVE_RUPEES, "liveRupees", "gCombo.Rando.Shared.LIVE_RUPEES", "Shared Rupees",
          "The rupees Link carries go with him to the other game, up to its wallet.", "", "", 0, false, false, false },
        { SF_LIVE_HEALTH, "liveHealth", "gCombo.Rando.Shared.LIVE_HEALTH", "Shared Health",
          "Link's health goes with him to the other game, up to its hearts.", "", "", 0, false, false, false },
        { SF_LIVE_MAGIC, "liveMagic", "gCombo.Rando.Shared.LIVE_MAGIC", "Shared Magic",
          "Link's magic, and Chateau Romani's infinite magic, go with him to the other game.", "", "", 0, false, false,
          false },
        { SF_LIVE_EQUIPS, "liveEquips", "gCombo.Rando.Shared.LIVE_EQUIPS", "Shared Equips",
          "The C buttons and the D-pad keep the items both games have when Link changes games.", "", "", 0, false,
          false, false },
        { SF_GOLDEN_HEART_PIECES, "goldenHearts", "gCombo.Rando.Shared.GOLDEN_HEARTS", "Shared Golden Hearts",
          "Every Golden Heart Container and Piece found counts for both games.", "nei.heart.golden_piece",
          "nei.heart.golden_piece", kSharedCopiesCap, false, true, false },
    };
    return table;
}

inline const SharedFamilyDef& SharedFamilyByIndex(int i) {
    return SharedFamilyTable()[i];
}

// OoT's Razor and Gilded tiers come from a mod chain that may be off: MM's own copies then carry the family.
inline bool CountsMmTiers(const SharedFamilyDef& family) {
    return family.family == SF_SWORD;
}

inline std::string SharedTierKey(int family) {
    switch (family) {
        case SF_HEART_PIECES:
            return "heartQuarters";
        case SF_HEART_CONTAINERS:
            return "heartContainers";
        case SF_GOLDEN_HEART_PIECES:
            return "goldenHeartPieces";
        case SF_DOUBLE_DEFENSE:
            return "doubleDefense";
        case SF_OCARINA_A:
            return "ocarinaA";
        case SF_OCARINA_C_UP:
            return "ocarinaCUp";
        case SF_OCARINA_C_DOWN:
            return "ocarinaCDown";
        case SF_OCARINA_C_LEFT:
            return "ocarinaCLeft";
        case SF_OCARINA_C_RIGHT:
            return "ocarinaCRight";
        case SF_STRENGTH:
            return "strength";
        default:
            return SharedFamilyByIndex(family).key;
    }
}

inline uint64_t SharedMaskFromKeys(const nlohmann::json& keys) {
    uint64_t mask = 0;
    if (!keys.is_array())
        return mask;
    for (const auto& k : keys) {
        if (!k.is_string())
            continue;
        const std::string s = k.get<std::string>();
        for (int i = 0; i < SF_COUNT; ++i)
            if (s == SharedFamilyTable()[i].key)
                mask |= (uint64_t{ 1 } << i);
    }
    return mask;
}

inline uint64_t SharedMaskAll() {
    return UINT64_MAX >> (64 - SF_COUNT);
}

// The options that carry what Link holds right now: no pool item backs them.
inline uint64_t SharedLiveStateMask() {
    uint64_t mask = 0;
    for (const int family : { SF_LIVE_RUPEES, SF_LIVE_HEALTH, SF_LIVE_MAGIC, SF_LIVE_EQUIPS }) {
        mask |= uint64_t{ 1 } << family;
    }
    return mask;
}

inline std::set<std::string> SharedUntaggedNames(uint64_t mask) {
    std::set<std::string> out;
    for (int i = 0; i < SF_COUNT; ++i) {
        if (!(mask & (uint64_t{ 1 } << i)))
            continue;
        out.insert(SharedFamilyTable()[i].ootName);
        if (SharedFamilyTable()[i].mmHasItem)
            out.insert(SharedFamilyTable()[i].mmName);
    }
    return out;
}

// One option may cover several families (Shared Health, Shields, Ocarina Buttons): each key appears once.
inline std::vector<std::string> SharedKeysFromMask(uint64_t mask) {
    std::vector<std::string> out;
    for (int i = 0; i < SF_COUNT; ++i) {
        const std::string key = SharedFamilyTable()[i].key;
        if ((mask & (uint64_t{ 1 } << i)) && std::find(out.begin(), out.end(), key) == out.end())
            out.push_back(key);
    }
    return out;
}

inline int SharedOptionFamily(const std::string& key) {
    for (int i = 0; i < SF_COUNT; ++i)
        if (key == SharedFamilyTable()[i].key)
            return i;
    return -1;
}

inline bool IsFirstOfSharedOption(int family) {
    return SharedOptionFamily(SharedFamilyTable()[family].key) == family;
}

inline constexpr int kNoSharedFamily = -1;

// A mod pair that is the same item as another share option. It follows that option instead of being a choice:
// a form mask rides with its trade mask, an upgrade chain's pickup with the item the chain starts from.
struct FoldedPair {
    const char* key;
    int hostFamily;
    const char* hostPair;
};

inline constexpr FoldedPair kFoldedPairs[] = {
    { "nei.goron_form_mask", SF_GORON_MASK, "" },
    { "nei.zora_form_mask", SF_ZORA_MASK, "" },
    { "nei.keaton_form_mask", SF_KEATON_MASK, "" },
    { "nei.mask_bunny", SF_BUNNY_HOOD, "" },
    { "nei.mask_truth", SF_MASK_OF_TRUTH, "" },
    { "nei.progressive.kokiri_sword", SF_SWORD, "" },
    { "nei.progressive.hookshot", SF_HOOKSHOT, "" },
    { "nei.progressive.hammer", kNoSharedFamily, "oot.hammer" },
    { "nei.progressive.master_sword", kNoSharedFamily, "oot.equip.master_sword" },
    { "nei.progressive.bgs", kNoSharedFamily, "oot.equip.biggoron_sword" },
};

inline const FoldedPair* FindFoldedPair(const std::string& key) {
    const auto found = std::find_if(std::begin(kFoldedPairs), std::end(kFoldedPairs),
                                    [&](const FoldedPair& folded) { return key == folded.key; });
    return found == std::end(kFoldedPairs) ? nullptr : found;
}

} // namespace ComboRando
