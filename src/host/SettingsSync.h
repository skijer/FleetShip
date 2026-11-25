#pragma once

#include <ship/GameRegistry.h>
#include <ship/config/ConsoleVariable.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

class SettingsSync {
  public:
    enum class Kind { Integer, Floating, Text, Percent };
    struct Pair {
        const char* oot;
        const char* mm;
        bool floating = false;
        float minimum = 0;
        float maximum = 0;
        Kind kind = Kind::Integer;
    };
    struct RandoPair {
        const char* oot;
        const char* mm;
        std::vector<int> mmOfOot;
        std::vector<int> ootOfMm;
    };
    static const std::vector<RandoPair>& RandoPairs() {
        const std::vector<int> anyArea = { 0, 1, 1, 1 };
        const std::vector<int> allAreas = { 0, 3 };
        const std::vector<int> keysToMm = { 2, 3, 1, 4, 5, 0 };
        const std::vector<int> keysToOot = { 5, 2, 0, 1, 3, 4 };
        std::vector<int> heartsToMm;
        std::vector<int> heartsToOot = { 0 };
        for (int hearts = 1; hearts <= 20; hearts++) {
            heartsToMm.push_back(hearts);
            heartsToOot.push_back(hearts - 1);
        }
        static const std::vector<RandoPair> pairs = {
            { "gRandoSettings.NoLogic", "gRando.Options.RO_LOGIC", { 0, 1 }, { 0, 1, 1, 0 } },
            { "gRandoSettings.ShuffleTokens", "gRando.Options.RO_SHUFFLE_GOLD_SKULLTULAS", anyArea, allAreas },
            { "gRandoSettings.ShuffleButterflyFairies", "gRando.Options.RO_SHUFFLE_BUTTERFLIES" },
            { "gRandoSettings.ShufflePots", "gRando.Options.RO_SHUFFLE_POT_DROPS", anyArea, allAreas },
            { "gRandoSettings.ShuffleCrates", "gRando.Options.RO_SHUFFLE_CRATE_DROPS", anyArea, allAreas },
            { "gRandoSettings.ShuffleGrass", "gRando.Options.RO_SHUFFLE_GRASS_DROPS", anyArea, allAreas },
            { "gRandoSettings.ShuffleFreestanding", "gRando.Options.RO_SHUFFLE_FREESTANDING_ITEMS", anyArea, allAreas },
            { "gRandoSettings.ShuffleWonderItems", "gRando.Options.RO_SHUFFLE_WONDER_ITEMS", anyArea, allAreas },
            { "gRandoSettings.ShuffleCows", "gRando.Options.RO_SHUFFLE_COWS" },
            { "gRandoSettings.ShuffleBeehives", "gRando.Options.RO_SHUFFLE_HIVE_DROPS" },
            { "gRandoSettings.ShuffleTrees", "gRando.Options.RO_SHUFFLE_TREE_DROPS" },
            { "gRandoSettings.ShuffleOcarinaButtons", "gRando.Options.RO_SHUFFLE_OCARINA_BUTTONS" },
            { "gRandoSettings.ShuffleSwim", "gRando.Options.RO_SHUFFLE_SWIM" },
            { "gRandoSettings.ShuffleBossSouls", "gRando.Options.RO_SHUFFLE_BOSS_SOULS" },
            { "gRandoSettings.ShuffleEnemySouls", "gRando.Options.RO_SHUFFLE_ENEMY_SOULS" },
            { "gRandoSettings.ShuffleEnemyDrops", "gRando.Options.RO_SHUFFLE_ENEMY_DROPS" },
            { "gRandoSettings.EnemyRandomizer", "gRando.Options.RO_ENEMY_RANDOMIZER" },
            { "gRandoSettings.EnemyRandomizerMinibosses", "gRando.Options.RO_ENEMY_RANDOMIZER_MINIBOSSES" },
            { "gRandoSettings.EnemySoulBehaviour", "gRando.Options.RO_ENEMY_SOUL_BEHAVIOUR" },
            { "gRandoSettings.SkeletonKey", "gRando.Options.RO_SHUFFLE_SKELETON_KEY" },
            { "gRandoSettings.IncludeTycoonWallet", "gRando.Options.RO_SHUFFLE_TYCOON_WALLET" },
            { "gRandoSettings.ItemPool", "gRando.Options.RO_PLENTIFUL_ITEMS", { 1, 0 }, { 1, 0 } },
            { "gRandoSettings.Keysanity", "gRando.Options.RO_PLACEMENT_SMALL_KEYS", keysToMm, keysToOot },
            { "gRandoSettings.BossKeysanity", "gRando.Options.RO_PLACEMENT_BOSS_KEYS", keysToMm, keysToOot },
            { "gRandoSettings.ShuffleKeyRings", "gRando.ModOptions.mm_keyrings.mode" },
            { "gRandoSettings.Mods.junk_cycle.mode", "gRando.JunkItems" },
            { "gRandoSettings.StartingMapsCompasses",
              "gRando.Options.RO_STARTING_MAPS_AND_COMPASSES",
              { 1, 0 },
              { 2, 0 } },
            { "gRandoSettings.GossipStoneHints", "gRando.Options.RO_HINTS_GOSSIP_STONES", { 0, 1 }, { 0, 1 } },
            { "gRandoSettings.ShuffleDungeonReward", "gRando.Options.RO_SHUFFLE_BOSS_REMAINS", { 0, 0, 1 }, { 1, 5 } },
            { "gRandoSettings.Shopsanity", "gRando.Options.RO_SHUFFLE_SHOPS", { 0, 1 }, { 0, 2 } },
            { "gRandoSettings.StartingHearts", "gRando.Options.RO_STARTING_HEALTH", heartsToMm, heartsToOot },
            { "gRandoSettings.FullWallets", "gRando.Options.RO_STARTING_RUPEES" },
            { "gEnhancements.ChestSizeAndTextureMatchContents", "gRando.CSMC" },
            { "gEnhancements.ChestSizeDependsStoneOfAgony", "gRando.ModOptions.oot.csmc_agony" },
            { "gRandoSettings.ShuffleOpenChest", "gRando.ModOptions.oot.shuffle_open_chest", { 0, 1 }, { 0, 1 } },
            { "gRandoSettings.ShuffleSpeak", "gRando.ModOptions.oot.shuffle_speak" },
            { "gRandoSettings.ShuffleClimb", "gRando.ModOptions.oot.shuffle_climb" },
            { "gRandoSettings.ShuffleGrab", "gRando.ModOptions.oot.shuffle_grab" },
            { "gRandoSettings.ShuffleChildWallet", "gRando.ModOptions.oot.shuffle_child_wallet" },
            { "gRandoSettings.ShuffleDekuStickBag", "gRando.ModOptions.oot.shuffle_deku_stick_bag" },
            { "gRandoSettings.ShuffleDekuNutBag", "gRando.ModOptions.oot.shuffle_deku_nut_bag" },
            { "gRandoSettings.BombchuBag", "gRando.ModOptions.oot.bombchu_bag", { 0, 1 }, { 0, 2 } },
            { "gRandoSettings.InfiniteUpgrades", "gRando.ModOptions.oot.infinite_upgrades", { 0, 1 }, { 0, 1 } },
            { "gRandoSettings.Mods.oot.golden_hearts", "gRando.ModOptions.oot.golden_hearts" },
            { "gRandoSettings.Mods.oot.golden_heart_pieces", "gRando.ModOptions.oot.golden_heart_pieces" },
            { "gRandoSettings.Mods.oot.starting_golden_hearts", "gRando.ModOptions.oot.starting_golden_hearts" },
            { "gRandoSettings.Mods.nei.somaria.shuffle", "gRando.ModOptions.nei.somaria.shuffle" },
            { "gRandoSettings.Mods.nei.wand_shuffle", "gRando.ModOptions.nei.wand_shuffle" },
            { "gRandoSettings.Mods.ultrahand_pieces", "gRando.ModOptions.ultrahand_pieces" },
            { "gRandoSettings.Mods.nei.bomb_arrows_auto", "gRando.ModOptions.nei.bomb_arrows_auto" },
            { "gEnhancements.ExtraTraps.Enabled", "gRando.Options.RO_SHUFFLE_TRAPS" },
            { "gRandoSettings.AdditionalIceTraps", "gRando.Options.RO_TRAP_AMOUNT" },
            { "gEnhancements.ExtraTraps.Ice", "gRando.Traps.Freeze" },
            { "gEnhancements.ExtraTraps.Burn", "gRando.Traps.Fire" },
            { "gEnhancements.ExtraTraps.Shock", "gRando.Traps.Shock" },
            { "gEnhancements.ExtraTraps.Knockback", "gRando.Traps.Knockback" },
            { "gEnhancements.ExtraTraps.Bomb", "gRando.Traps.Blast" },
            { "gEnhancements.ExtraTraps.Speed", "gRando.Traps.Speed" },
            { "gEnhancements.ExtraTraps.Void", "gRando.Traps.Void" },
            { "gEnhancements.ExtraTraps.Ammo", "gRando.Traps.Ammo" },
            { "gEnhancements.ExtraTraps.Kill", "gRando.Traps.Kill" },
            { "gEnhancements.ExtraTraps.Teleport", "gRando.Traps.Teleport" },
            { "gEnhancements.ExtraTraps.Jinx", "gRando.Traps.Jinx" },
            { "gEnhancements.ExtraTraps.Wallet", "gRando.Traps.Wallet" },
            { "gEnhancements.ExtraTraps.Enemy", "gRando.Traps.Enemy" },
            { "gEnhancements.ExtraTraps.Time", "gRando.Traps.Time" },
            { "gRandoEnhancements.TimeSavers.SkipGetItemAnimation",
              "gEnhancements.Cutscenes.SkipGetItemCutscenes",
              { 0, 1, 3 },
              { 0, 1, 1, 2 } },
        };
        return pairs;
    }
    static const auto& Pairs() {
        static const Pair pairs[] = {
            { "gRemote.Anchor.Host", "gNetwork.Anchor.Host", false, 0, 0, Kind::Text },
            { "gRemote.Anchor.Port", "gNetwork.Anchor.Port" },
            { "gRemote.Anchor.RoomId", "gNetwork.Anchor.RoomId", false, 0, 0, Kind::Text },
            { "gRemote.Anchor.TeamId", "gNetwork.Anchor.TeamId", false, 0, 0, Kind::Text },
            { "gRemote.Anchor.Name", "gNetwork.Anchor.Name", false, 0, 0, Kind::Text },
            { "gRemote.Anchor.Enabled", "gNetwork.Anchor.Enabled" },
            { "gRandoSettings.EnemyPoolExcluded", "gRando.EnemyPoolExcluded", false, 0, 0, Kind::Text },
            { "gRandoSettings.SoulPoolExcluded", "gRando.SoulPoolExcluded", false, 0, 0, Kind::Text },
            { "gSettings.Menu.Theme", "gSettings.Menu.Theme" },
            { "gSettings.ImGuiScale", "gSettings.ImGuiScale" },
            { "gSettings.CursorVisibility", "gSettings.CursorVisibility" },
            { "gSettings.AltAssets", "gEnhancements.Mods.AlternateAssets" },
            { "gSettings.Mods.AlternateAssetsHotkey", "gEnhancements.Mods.AlternateAssetsHotkey" },
            { "gSettings.A11yNoScreenFlashForFinishingBlow", "gEnhancements.A11y.NoScreenFlashForEnemyKill" },
            { "gSettings.Notifications.Mute", "gNotifications.Mute" },
            { "gTimeDisplay.ShowWindowBG", "gDisplayOverlay.Background" },
            { "gTimeDisplay.FontScale", "gDisplayOverlay.Scale", true, 1, 5 },
            { "gSettings.Menu.SidebarSearch", "gSettings.Menu.SidebarSearch" },
            { "gSettings.Menu.SearchAutofocus", "gSettings.Menu.SearchAutofocus" },
            { "gSettings.Menu.BackgroundOpacity", "gSettings.Menu.BackgroundOpacity", true, 0, 1 },
            { "gSettings.ResetBtn", "gSettings.ResetBtn" },
            { "gSettings.InterpolationFPS", "gInterpolationFPS" },
            { "gSettings.MatchRefreshRate", "gMatchRefreshRate" },
            { "gSettings.Notifications.Position", "gNotifications.Position" },
            { "gSettings.Notifications.Duration", "gNotifications.Duration", true },
            { "gSettings.Notifications.BgOpacity", "gNotifications.BgOpacity", true },
            { "gSettings.Notifications.Size", "gNotifications.Size", true },
            { "gSettings.Volume.Master", "gSettings.Audio.MasterVolume", false, 0, 0, Kind::Percent },
            { "gSettings.Volume.MainMusic", "gSettings.Audio.MainMusicVolume", false, 0, 0, Kind::Percent },
            { "gSettings.Volume.SubMusic", "gSettings.Audio.SubMusicVolume", false, 0, 0, Kind::Percent },
            { "gSettings.Volume.Fanfare", "gSettings.Audio.FanfareVolume", false, 0, 0, Kind::Percent },
            { "gSettings.Volume.SFX", "gSettings.Audio.SoundEffectsVolume", false, 0, 0, Kind::Percent },
            { "gDeveloperTools.DebugEnabled", "gDeveloperTools.DebugEnabled" },
            { "gDeveloperTools.MapSelectBtn", "gDeveloperTools.MapSelectBtn" },
            { "gSettings.FreeLook.Enabled", "gEnhancements.Camera.FreeLook.Enable" },
            { "gSettings.FreeLook.InvertXAxis", "gEnhancements.Camera.RightStick.InvertXAxis" },
            { "gSettings.FreeLook.InvertYAxis", "gEnhancements.Camera.RightStick.InvertYAxis" },
            { "gSettings.FreeLook.CameraSensitivity.X", "gEnhancements.Camera.RightStick.CameraSensitivity.X", true,
              .01f, 5 },
            { "gSettings.FreeLook.CameraSensitivity.Y", "gEnhancements.Camera.RightStick.CameraSensitivity.Y", true,
              .01f, 5 },
            { "gSettings.FreeLook.MaxCameraDistance", "gEnhancements.Camera.FreeLook.MaxCameraDistance", false, 100,
              900 },
            { "gSettings.FreeLook.TransitionSpeed", "gEnhancements.Camera.FreeLook.TransitionSpeed", false, 1, 900 },
            { "gSettings.Controls.InvertAimingXAxis", "gEnhancements.Camera.FirstPerson.InvertX" },
            { "gSettings.Controls.InvertAimingYAxis", "gEnhancements.Camera.FirstPerson.InvertY" },
            { "gSettings.Controls.RightStickAim", "gEnhancements.Camera.FirstPerson.RightStickEnabled" },
            { "gSettings.MoveInFirstPerson", "gEnhancements.Camera.FirstPerson.MoveInFirstPerson" },
            { "gSettings.DisableFirstPersonAutoCenterView",
              "gEnhancements.Camera.FirstPerson.DisableFirstPersonAutoCenterView" },
            { "gSettings.FirstPersonCameraSensitivity.X", "gEnhancements.Camera.FirstPerson.SensitivityX", true, .01f,
              2 },
            { "gSettings.FirstPersonCameraSensitivity.Y", "gEnhancements.Camera.FirstPerson.SensitivityY", true, .01f,
              2 },
            { "gEnhancements.DpadEquips", "gEnhancements.Dpad.DpadEquips" },
            { "gEnhancements.ItemUnequip", "gEnhancements.Equipment.ItemUnequip" },
            { "gEnhancements.InstantPutaway", "gEnhancements.Player.InstantPutaway" },
            { "gEnhancements.UnsheatheWithoutSlashing", "gEnhancements.Player.UnsheatheWithoutSlashing" },
            { "gSettings.CustomOcarina.Enabled", "gEnhancements.Playback.CustomizeOcarinaControls" },
            { "gSettings.CustomOcarina.Dpad", "gEnhancements.Playback.DpadOcarina" },
            { "gSettings.CustomOcarina.RightStick", "gEnhancements.Playback.RightStickOcarina" },
            { "gEnhancements.DpadNoDropOcarinaInput", "gEnhancements.Playback.NoDropOcarinaInput" },
            { "gEnhancements.FastOcarinaPlayback", "gEnhancements.Songs.FasterSongPlayback" },
            { "gEnhancements.BowArrowCycle", "gEnhancements.PlayerActions.ArrowCycle" },
            { "gEnhancements.RemoteBombchu", "gEnhancements.PlayerActions.RemoteBombchu" },
            { "gEnhancements.RemoveExplosiveLimit", "gEnhancements.Items.RemoveExplosiveLimit" },
            { "gCheats.InfiniteHealth", "gCheats.InfiniteHealth" },
            { "gCheats.InfiniteMagic", "gCheats.InfiniteMagic" },
            { "gCheats.InfiniteMoney", "gCheats.InfiniteRupees" },
            { "gCheats.InfiniteAmmo", "gCheats.InfiniteConsumables" },
            { "gCheats.InfiniteEponaBoost", "gCheats.InfiniteEponaCarrots" },
            { "gCheats.NoRestrictItems", "gCheats.UnrestrictedItems" },
            { "gCheats.HookshotEverything", "gCheats.HookshotAnywhere" },
            { "gCheats.ClimbEverything", "gCheats.ClimbAnywhere" },
            { "gCheats.NoClip", "gCheats.NoClip" },
            { "gCheats.MoonJumpOnL", "gCheats.MoonJumpOnL" },
            { "gDeveloperTools.NoClipBtn", "gDeveloperTools.NoClipBtn" },
            { "gEnhancements.PermanentHeartLoss", "gEnhancements.DifficultyOptions.PermanentHeartLoss" },
            { "gEnhancements.DeleteFileOnDeath", "gEnhancements.DifficultyOptions.DeleteFileOnDeath" },
            { "gEnhancements.HyperEnemies", "gEnhancements.DifficultyOptions.HyperEnemies" },
            { "gEnhancements.NoHeartDrops", "gEnhancements.DifficultyOptions.NoHeartDrops" },
            { "gEnhancements.NoRandomDrops", "gEnhancements.DifficultyOptions.NoRandomDrops" },
            { "gEnhancements.RememberSaveLocation", "gEnhancements.Saving.RememberSaveLocation" },
            { "gEnhancements.TimeSavers.SkipCutscene.Entrances", "gEnhancements.Cutscenes.SkipEntranceCutscenes" },
            { "gEnhancements.TimeSavers.SkipCutscene.Story", "gEnhancements.Cutscenes.SkipStoryCutscenes" },
            { "gEnhancements.TimeSavers.SkipMiscInteractions", "gEnhancements.Cutscenes.SkipMiscInteractions" },
            { "gEnhancements.DisableBombBillboarding", "gEnhancements.Mods.DisableBombBillboarding" },
            { "gEnhancements.DisableGrottoRotation", "gEnhancements.Mods.DisableGrottoRotation" },
            { "gEnhancements.DisableDrawDistance", "gEnhancements.Graphics.IncreaseActorDrawDistance", false, 1, 5 },
            { "gEnhancements.FastChests", "gEnhancements.Timesavers.FastChests" },
            { "gEnhancements.FasterRupeeAccumulator", "gEnhancements.Timesavers.FasterRupeeAccumulator" },
            { "gEnhancements.InstantScarecrow", "gEnhancements.Playback.SkipScarecrowSong" },
            { "gEnhancements.FastBottles", "gEnhancements.Dialogue.SkipBottlePickupMessages" },
            { "gEnhancements.DisableBlackBars", "gEnhancements.Graphics.DisableBlackBars" },
            { "gEnhancements.WidescreenActorCulling", "gEnhancements.Graphics.ActorCullingAccountsForWidescreen" },
            { "gEnhancements.EnemyHealthBar", "gEnhancements.Graphics.EnemyHealthBars" },
            { "gEnhancements.BowReticle", "gEnhancements.Graphics.BowReticle" },
            { "gAudioEditor.DisableNaviCallAudio", "gAudioEditor.DisableTatlCallAudio" },
        };
        return pairs;
    }
    void Poll(const std::string& active, const std::function<void(const std::string&, const std::string&)>& apply) {
        auto oot = Ship::GameRegistry::Get("oot"), mm = Ship::GameRegistry::Get("mm");
        if (!oot || !mm) {
            return;
        }
        auto a = oot->ConsoleVariables, b = mm->ConsoleVariables;
        bool dirty = false;
        for (const auto& pair : Pairs()) {
            auto av = Read(*a, pair.oot, KindOf(pair, 0)), bv = Read(*b, pair.mm, KindOf(pair, 1));
            auto& last = mLast[pair.oot];
            const int source = ChangedSide(last, av, bv, active);
            if (source >= 0) {
                auto value = source == 0 ? av : bv;
                if (!value.is_null() && pair.minimum != pair.maximum) {
                    value = std::clamp(value.get<float>(), pair.minimum, pair.maximum);
                }
                Write(*a, pair.oot, KindOf(pair, 0), value);
                Write(*b, pair.mm, KindOf(pair, 1), value);
                if (pair.floating && std::string(pair.oot).starts_with("gSettings.FirstPersonCameraSensitivity.")) {
                    a->SetInteger("gSettings.FirstPersonCameraSensitivity.Enabled", !value.is_null());
                }
                apply("oot", pair.oot);
                apply("mm", pair.mm);
                dirty = true;
            }
            last = { Read(*a, pair.oot, KindOf(pair, 0)), Read(*b, pair.mm, KindOf(pair, 1)), true };
        }
        for (const auto& pair : RandoPairs()) {
            dirty |= SyncRando(pair, *a, *b, active, apply);
        }
        if (dirty) {
            a->Save();
            b->Save();
        }
    }

  private:
    struct Shadow {
        nlohmann::json a, b;
        bool armed = false;
    };
    std::map<std::string, Shadow> mLast;
    static int ChangedSide(const Shadow& last, const nlohmann::json& av, const nlohmann::json& bv,
                           const std::string& active) {
        if (!last.armed) {
            if (!av.is_null()) {
                return 0;
            }
            return bv.is_null() ? -1 : 1;
        }
        int source = -1;
        if (av != last.a) {
            source = 0;
        }
        if (bv != last.b && (source < 0 || active == "mm")) {
            source = 1;
        }
        return source;
    }
    static nlohmann::json Translate(const std::vector<int>& values, const nlohmann::json& value) {
        if (value.is_null() || values.empty()) {
            return value;
        }
        const size_t index = static_cast<size_t>(std::max(0, value.get<int>()));
        return values[std::min(index, values.size() - 1)];
    }
    bool SyncRando(const RandoPair& pair, Ship::ConsoleVariable& a, Ship::ConsoleVariable& b, const std::string& active,
                   const std::function<void(const std::string&, const std::string&)>& apply) {
        auto& last = mLast[pair.oot];
        const int source = ChangedSide(last, Read(a, pair.oot, Kind::Integer), Read(b, pair.mm, Kind::Integer), active);
        if (source == 0) {
            Write(b, pair.mm, Kind::Integer, Translate(pair.mmOfOot, Read(a, pair.oot, Kind::Integer)));
        } else if (source == 1) {
            Write(a, pair.oot, Kind::Integer, Translate(pair.ootOfMm, Read(b, pair.mm, Kind::Integer)));
        }
        last = { Read(a, pair.oot, Kind::Integer), Read(b, pair.mm, Kind::Integer), true };
        if (source < 0) {
            return false;
        }
        apply("oot", pair.oot);
        apply("mm", pair.mm);
        return true;
    }
    static Kind KindOf(const Pair& pair, int side) {
        if (pair.floating) {
            return Kind::Floating;
        }
        if (pair.kind == Kind::Percent) {
            return side == 0 ? Kind::Percent : Kind::Floating;
        }
        return pair.kind;
    }
    // A text pair only reads a string variable: GetString on another type reads its union as a pointer.
    static nlohmann::json Read(Ship::ConsoleVariable& vars, const char* key, Kind kind) {
        const auto variable = vars.Get(key);
        if (!variable) {
            return nullptr;
        }
        if (kind == Kind::Floating) {
            return vars.GetFloat(key, 0);
        }
        if (kind == Kind::Percent) {
            return vars.GetInteger(key, 0) / 100.0f;
        }
        if (kind == Kind::Text) {
            return variable->Type == Ship::ConsoleVariableType::String ? nlohmann::json(vars.GetString(key, ""))
                                                                       : nullptr;
        }
        return vars.GetInteger(key, 0);
    }
    static void Write(Ship::ConsoleVariable& vars, const char* key, Kind kind, const nlohmann::json& value) {
        if (value.is_null()) {
            vars.ClearVariable(key);
        } else if (kind == Kind::Floating) {
            vars.SetFloat(key, value.get<float>());
        } else if (kind == Kind::Text) {
            vars.SetString(key, value.get<std::string>().c_str());
        } else if (kind == Kind::Percent) {
            vars.SetInteger(key, static_cast<int>(std::lround(value.get<float>() * 100)));
        } else {
            vars.SetInteger(key, value.get<int>());
        }
    }
};
