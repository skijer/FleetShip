#include "FleetGui.h"
#include "FleetSaveMenu.h"
#include "SettingsSync.h"
#include "gui/ComboWidgetStyle.h"
#include <ship/Context.h>
#include <ship/GameRegistry.h>
#include <ship/window/Window.h>
#include <ship/config/ConsoleVariable.h>
#include <ship/window/gui/IconsFontAwesome4.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <set>
#include <iostream>

namespace {
class LoadingOverlay final : public Ship::GuiWindow {
  public:
    explicit LoadingOverlay(std::shared_ptr<std::string> text)
        : GuiWindow("", false, "~Fleet Loading"), mText(std::move(text)) {
    }
    void InitElement() override {
    }
    void UpdateElement() override {
    }
    void DrawElement() override {
    }
    void Draw() override {
        if (!IsVisible()) {
            return;
        }
        ImGui::SetCurrentContext(Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetImGuiContext());
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
        ImGui::Begin("Fleet Loading", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoInputs);
        ImGui::SetCursorPos(ImVec2(std::max(16.0f, (viewport->Size.x - ImGui::CalcTextSize(mText->c_str()).x) / 2),
                                   viewport->Size.y / 2));
        ImGui::TextUnformatted(mText->c_str());
        ImGui::End();
        ImGui::PopStyleColor();
    }

  private:
    std::shared_ptr<std::string> mText;
};

// The item review's banner along the top, clear of the get-item textbox at the bottom.
class ReviewOverlay final : public Ship::GuiWindow {
  public:
    explicit ReviewOverlay(std::shared_ptr<FleetGui::ReviewPrompt> prompt)
        : GuiWindow("", false, "~Fleet Review"), mPrompt(std::move(prompt)) {
    }
    void InitElement() override {
    }
    void UpdateElement() override {
    }
    void DrawElement() override {
    }
    void Draw() override {
        if (!IsVisible()) {
            return;
        }
        constexpr float kIconSize = 64.0f;
        constexpr float kMargin = 12.0f;
        ImGui::SetCurrentContext(Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetImGuiContext());
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x / 2, viewport->Pos.y + kMargin),
                                ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.8f);
        ImGui::Begin("Fleet Review", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoFocusOnAppearing);
        FleetItemIcon icon = {};
        if (mPrompt->findIcon && mPrompt->findIcon(mPrompt->game, mPrompt->item, icon)) {
            ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(reinterpret_cast<uintptr_t>(icon.texture)),
                               ImVec2(kIconSize, kIconSize), ImVec2(icon.uv0[0], icon.uv0[1]),
                               ImVec2(icon.uv1[0], icon.uv1[1]), ImVec4(0, 0, 0, 0),
                               ImVec4(icon.tint[0], icon.tint[1], icon.tint[2], icon.tint[3]));
        } else {
            ImGui::Dummy(ImVec2(kIconSize, kIconSize));
        }
        ImGui::SameLine();
        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextUnformatted(mPrompt->text.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::End();
    }

  private:
    std::shared_ptr<FleetGui::ReviewPrompt> mPrompt;
};

class OwnedWindow final : public Ship::GuiWindow {
  public:
    OwnedWindow(std::string owner, std::shared_ptr<Ship::GuiWindow> window)
        : GuiWindow("", window->IsVisible(), window->GetName()), mOwner(std::move(owner)), mWindow(std::move(window)) {
    }
    void InitElement() override {
    }
    void UpdateElement() override {
        Ship::GameScope scope(mOwner);
        mWindow->Update();
    }
    void DrawElement() override {
        Ship::GameScope scope(mOwner);
        mWindow->DrawElement();
    }
    void Draw() override {
        Ship::GameScope scope(mOwner);
        mWindow->Draw();
        Ship::GuiElement::SetVisibility(mWindow->IsVisible());
    }

  protected:
    void SetVisibility(bool visible) override {
        Ship::GameScope scope(mOwner);
        if (visible) {
            mWindow->Show();
        } else {
            mWindow->Hide();
        }
        Ship::GuiElement::SetVisibility(visible);
    }

  private:
    std::string mOwner;
    std::shared_ptr<Ship::GuiWindow> mWindow;
};

struct Rows {
    bool only;
    std::set<std::string> names;
    bool Keeps(const std::string& name) const {
        return names.contains(name) == only;
    }
    bool Absorbed() const {
        return only && names.empty();
    }
};
Rows All() {
    return { false, {} };
}
Rows Only(std::set<std::string> names) {
    return { true, std::move(names) };
}
Rows Except(std::set<std::string> names) {
    return { false, std::move(names) };
}
Rows Absorbed() {
    return { true, {} };
}

struct PagePart {
    const char* game;
    const char* section;
    const char* sidebar;
    const char* heading;
    Rows rows;
};
struct SettingsPage {
    const char* group;
    const char* label;
    std::vector<PagePart> parts;
};

const std::set<std::string> kSharedGeneral = { "Menu Settings",
                                               "Menu Theme",
                                               "Menu Controller Navigation",
                                               "Allow background inputs",
                                               "Menu Background Opacity",
                                               "General Settings",
                                               "Cursor Always Visible",
                                               "Search In Sidebar",
                                               "Search Input Autofocus",
                                               "Reset Button Combination:",
                                               "Disable Screen Flash for Finishing Blow",
                                               "EXPERIMENTAL",
                                               "ImGui Menu Scaling" };
const std::set<std::string> kSharedTimers = { "Font Scale: %.2fx", "Hide Background" };

const std::vector<SettingsPage>& SettingsPages() {
    static const std::vector<SettingsPage> pages = {
        { "Settings",
          "General",
          { { "oot", "Settings", "General", nullptr, Only(kSharedGeneral) },
            { "mm", "Settings", "General", nullptr, Only({ "Alt Assets Tab hotkey" }) },
            { "oot", "Settings", "General", "OoT only", Except(kSharedGeneral) },
            { "mm", "Settings", "General", "MM only", Only({ "Open App Files Folder", "about" }) } } },
        { "Settings",
          "Audio",
          { { "oot", "Settings", "Audio", nullptr, All() },
            { "mm", "Settings", "Audio", "MM only", Only({ "Ambience Volume: %.0f%%" }) } } },
        { "Settings",
          "Graphics",
          { { "oot", "Settings", "Graphics", nullptr, All() },
            { "mm", "Settings", "Graphics", nullptr, Absorbed() } } },
        { "Settings",
          "Controls",
          { { "oot", "Settings", "Controls", nullptr, All() },
            { "mm", "Settings", "Controls", nullptr, Absorbed() } } },
        { "Settings",
          "Input Viewer",
          { { "oot", "Settings", "Input Viewer", nullptr, All() },
            { "mm", "Settings", "Input Viewer", nullptr, Absorbed() } } },
        { "Settings",
          "Overlay",
          { { "oot", "Settings", "Notifications", "Notifications", All() },
            { "oot", "Enhancements", "Timers", "Timers", Only(kSharedTimers) },
            { "oot", "Enhancements", "Timers", "OoT only", Except(kSharedTimers) },
            { "mm", "Settings", "Overlay", "MM only", Only({ "In-Game Timer", "Display" }) } } },
        { "Mods", "OoT Mods", { { "oot", "Settings", "Mod Menu", nullptr, All() } } },
        { "Mods", "MM Mods", { { "mm", "Settings", "Mod Menu", nullptr, All() } } },
        { "Presets", "OoT Presets", { { "oot", "Settings", "Presets", nullptr, All() } } },
        { "Presets", "MM Presets", { { "mm", "Settings", "Presets", nullptr, All() } } },
    };
    return pages;
}

nlohmann::json KeptWidgets(nlohmann::json sidebar, const std::function<bool(const nlohmann::json&)>& keeps) {
    auto& widgets = sidebar.at("widgets");
    widgets.erase(std::remove_if(widgets.begin(), widgets.end(), std::not_fn(keeps)), widgets.end());
    return sidebar;
}

nlohmann::json KeptRows(nlohmann::json sidebar, const Rows& rows) {
    return KeptWidgets(std::move(sidebar),
                       [&](const nlohmann::json& widget) { return rows.Keeps(widget.value("name", "")); });
}

const char* GameTitle(const std::string& game) {
    return game == "oot" ? "Ocarina of Time" : "Majora's Mask";
}

constexpr const char* kBothGames = "Both games";

bool IsSharedOotRow(const nlohmann::json& widget) {
    static const auto shared = [] {
        std::set<std::string> cvars;
        for (const auto& pair : SettingsSync::RandoPairs()) {
            cvars.insert(pair.oot);
        }
        return cvars;
    }();
    return shared.contains(widget.value("cvar", ""));
}

bool IsRandomizerSection(const std::string& section) {
    return section == "Randomizer" || section == "Rando";
}

bool IsOotModOption(const std::string& cvar) {
    return cvar.starts_with("gRandoSettings.Mods.");
}

const std::set<std::string> kOotOptionGroups = { "Seed Entry",
                                                 "Enhancements",
                                                 "Logic",
                                                 "Win Condition",
                                                 "Area Access",
                                                 "Entrances",
                                                 "Dungeon Items",
                                                 "Keyrings",
                                                 "Master Quest",
                                                 "Shuffle Items",
                                                 "Shuffle Shops & Merchants",
                                                 "Additional Items",
                                                 "Hints",
                                                 "Traps",
                                                 "Static Hints" };

bool IsMmFrameWidget(const nlohmann::json& widget) {
    const std::string name = widget.value("name", "");
    return name.starts_with("Layout") || name.starts_with("Section");
}

const std::set<std::string> kRowsDrawnByFleet = { "gRandoSettings.ShuffleKeyRingsRandomCount",
                                                  "gRando.ModOptions.mm_keyrings.count",
                                                  "gRandoSettings.ShuffleKokiriSword",
                                                  "gRandoSettings.ShuffleMasterSword",
                                                  "gRandoSettings.StartingMapsCompasses",
                                                  "gRandoSettings.Keysanity",
                                                  "gRandoSettings.BossKeysanity",
                                                  "gRandoSettings.ShuffleDungeonReward",
                                                  "gRandoSettings.GerudoKeys",
                                                  "gRando.Options.RO_PLACEMENT_STRAY_FAIRIES",
                                                  "gRandoSettings.ShuffleDungeonsEntrances",
                                                  "gRandoSettings.ShuffleBossEntrances",
                                                  "gRandoSettings.ShuffleInteriorsEntrances",
                                                  "gRandoSettings.ShuffleGrottosEntrances",
                                                  "gRandoSettings.TriforceHuntTotalPieces",
                                                  "gRandoSettings.TriforceHuntPiecesLocation",
                                                  "gRandoSettings.ShuffleWincon",
                                                  "gRandoSettings.WinconRewardOptions" };
constexpr std::string_view kOotWinconCountPrefix = "gRandoSettings.Wincon";

bool IsDrawnByFleet(const std::string& cvar) {
    return kRowsDrawnByFleet.contains(cvar) || cvar.starts_with(kOotWinconCountPrefix);
}

constexpr std::string_view kMmKeyRingDungeonPrefix = "gRando.ModOptions.mm_keyrings.";
constexpr int kKeyRingSelection = 3;

bool IsHiddenByCurrentMode(const std::string& cvar) {
    if (!cvar.starts_with(kMmKeyRingDungeonPrefix) || cvar.ends_with(".mode")) {
        return false;
    }
    const auto oot = Ship::GameRegistry::Get("oot");
    return oot && oot->ConsoleVariables->GetInteger("gRandoSettings.ShuffleKeyRings", 0) != kKeyRingSelection;
}

const std::set<std::string> kRowsForcedOn = { "gRandoSettings.ShuffleMasks" };

const std::vector<std::string> kMmRowsOwnedByFleet = { "gRando.Options.RO_SHUFFLE_TRIFORCE_PIECES",
                                                       "gRando.Options.RO_TRIFORCE_PIECES_REQUIRED",
                                                       "gRando.Options.RO_TRIFORCE_PIECES_MAX",
                                                       "Fleet.SeedGeneration",
                                                       "Fleet.SeedHealth",
                                                       "Fleet.ComingSoon",
                                                       "Fleet.StartingItems",
                                                       "gRando.Options.RO_SHUFFLE_SONG_SUN" };

struct RowPlace {
    const char* page;
    const char* heading;
};

struct RowRoute {
    const char* game;
    const char* section;
    const char* sidebar;
    const char* group = nullptr;
    const char* cvarPrefix = nullptr;
    std::vector<RowPlace> places;
    bool sharedOnly = false;
    std::set<std::string> names = {};
};

const std::vector<RowRoute>& RowRoutes() {
    static const std::vector<RowRoute> routes = {
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "General",
          .group = "Enhancements",
          .places = { { "General", "Enhancements" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Logic/Access",
          .cvarPrefix = "gRandoSettings.ItemPool",
          .places = { { "Item Pool", "Item Pool" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Logic/Access",
          .group = "Logic",
          .places = { { "Logic & Access", "Logic" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Logic/Access",
          .cvarPrefix = "gRandoSettings.ShuffleGanonsSoul",
          .places = { { "Logic & Access", "Souls" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Logic/Access",
          .cvarPrefix = "gRandoSettings.GanonsSoul",
          .places = { { "Logic & Access", "Souls" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Logic/Access",
          .group = "Win Condition",
          .places = { { "Logic & Access", "Win Gates" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Logic/Access",
          .group = "Area Access",
          .places = { { "Logic & Access", "Access" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Logic/Access",
          .group = "Entrances",
          .places = { { "Shuffles", "Entrances" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Dungeons",
          .cvarPrefix = "gRandoSettings.ShuffleBossSouls",
          .places = { { "Logic & Access", "Souls" }, { "Enemies", "Options" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Dungeons",
          .cvarPrefix = "gRandoSettings.GerudoKeys",
          .places = { { "Logic & Access", "Dungeon Items" }, { "Shuffles", "Keys" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Dungeons",
          .group = "Dungeon Items",
          .places = { { "Logic & Access", "Dungeon Items" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Dungeons",
          .group = "Keyrings",
          .places = { { "Logic & Access", "Key Rings" }, { "Shuffles", "Keys" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Dungeons",
          .group = "Master Quest",
          .places = { { "Logic & Access", "Master Quest" }, { "Shuffles", "Master Quest" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Shuffles",
          .cvarPrefix = "gRandoSettings.ShuffleEnemySouls",
          .places = { { "Logic & Access", "Souls" }, { "Enemies", "Options" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Shuffles",
          .group = "Shuffle Items",
          .places = { { "Shuffles", "Shuffle Items" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Shuffles",
          .group = "Shuffle Shops & Merchants",
          .places = { { "Shuffles", "Shops & Merchants" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Shuffles",
          .cvarPrefix = "gRandoSettings.EnemyRandomizer",
          .places = { { "Enemies", "Options" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Shuffles",
          .cvarPrefix = "gRandoSettings.EnemySoulBehaviour",
          .places = { { "Enemies", "Options" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Enemy Pools",
          .places = { { "Enemies", "Ocarina of Time" } } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Enemies",
          .places = { { "Enemies", "Majora's Mask" } },
          .names = { "Enemy Pool", "Miniboss Pool", "Soul Pool" } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Shuffles",
          .group = "Additional Items",
          .places = { { "Item Pool", "Additional Items" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Hints/Traps",
          .group = "Hints",
          .places = { { "Hints", "Hints" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Hints/Traps",
          .group = "Traps",
          .places = { { "Item Pool", "Traps" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Hints/Traps",
          .group = "Static Hints",
          .places = { { "Hints", "Static Hints" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Tricks/Glitches",
          .places = { { "Tricks/Glitches", "" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Mods",
          .cvarPrefix = "gRandoSettings.Mods.junk_cycle.",
          .places = { { "General", "Enhancements" } } },
        { .game = "oot",
          .section = "Randomizer",
          .sidebar = "Mods",
          .cvarPrefix = "gRandoSettings.Mods.nei.shuffle.",
          .places = { { "Shuffles", "Extra Items" }, { "Item Pool", "Additional Items" } } },
        { .game = "oot", .section = "Randomizer", .sidebar = "Mods", .places = { { "Mods", "" } } },
        { .game = "oot",
          .section = "Enhancements",
          .sidebar = "Quality of Life",
          .places = { { "General", "Enhancements" } },
          .sharedOnly = true },
        { .game = "oot",
          .section = "Enhancements",
          .sidebar = "Extra Modes",
          .cvarPrefix = "gEnhancements.ExtraTraps.",
          .places = { { "Item Pool", "Traps" } } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "General",
          .places = { { "General", "Enhancements" } },
          .names = { "Container Style Matches Contents", "Junk Items" } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Logic/Conditions",
          .places = { { "Logic & Access", "Logic" } },
          .names = { "Logic" } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Logic/Conditions",
          .places = { { "Logic & Access", "Access" } },
          .names = { "Dungeon Access", "Trials Access" } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Logic/Conditions",
          .places = { { "Logic & Access", "Win Gates" } },
          .names = { "Majora Access Remains Required", "Majora Access Masks Required", "Moon Access Remains Required",
                     "Moon Access Masks Required", "Moon Access Triforce Pieces Required" } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Check Pool",
          .cvarPrefix = "gRando.Options.",
          .places = { { "Shuffles", "Check Pool" } } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Check Pool",
          .places = { { "Shuffles", "Check Pool" } },
          .names = { "Scenery Drops Toggles" } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Item Pool",
          .places = { { "Logic & Access", "Dungeon Items" } },
          .names = { "Small Keys", "Boss Keys", "Stray Fairies", "Required Stray Fairies", "Stray Fairies in Pool" } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Item Pool",
          .places = { { "Logic & Access", "Souls" } },
          .names = { "Boss Souls", "Majora's Soul" } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Item Pool",
          .cvarPrefix = "gRando.Options.RO_MAJORA_SOUL_",
          .places = { { "Logic & Access", "Souls" } } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Item Pool",
          .places = { { "Item Pool", "Item Pool" } },
          .names = { "Plentiful Items" } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Item Pool",
          .places = { { "Item Pool", "Traps" } },
          .names = { "Traps", "Trap Amount", "Trap Behavior", "Trap Types" } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Item Pool",
          .places = { { "Shuffles", "Extra Items" } },
          .names = { "Swim", "Ocarina Buttons", "Skeleton Key", "Tycoon's Wallet", "Repeatable Rupee Purchases",
                     "Song of Double Time", "Inverted Song of Time", "Saria's Song", "Enemy Souls", "Shuffle Time",
                     "Time Progression Mode", "Final Hours Start Time" } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Hints",
          .cvarPrefix = "gRando.Options.",
          .places = { { "Hints", "Hints" } } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Mods",
          .cvarPrefix = "gRando.ModOptions.mm_keyrings.",
          .places = { { "Logic & Access", "Key Rings" }, { "Shuffles", "Keys" } } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Mods",
          .cvarPrefix = "gRando.ModOptions.unbound.",
          .places = { { "Shuffles", "Entrances" } } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Mods",
          .cvarPrefix = "gRando.ModOptions.oot.shuffle_",
          .places = { { "Shuffles", "Extra Items" }, { "Item Pool", "Additional Items" } } },
        { .game = "mm",
          .section = "Rando",
          .sidebar = "Mods",
          .cvarPrefix = "gRando.ModOptions.oot.slingshot.",
          .places = { { "Shuffles", "Extra Items" }, { "Item Pool", "Additional Items" } } },
        { .game = "mm", .section = "Rando", .sidebar = "Mods", .places = { { "Mods", "" } } },
    };
    return routes;
}

bool IsRouteFor(const RowRoute& route, const std::string& game, const std::string& section, const std::string& sidebar,
                const nlohmann::json& widget) {
    if (game != route.game || section != route.section || sidebar != route.sidebar) {
        return false;
    }
    if (route.group != nullptr && widget.value("group", "") != route.group) {
        return false;
    }
    if (route.cvarPrefix != nullptr && !widget.value("cvar", "").starts_with(route.cvarPrefix)) {
        return false;
    }
    if (!route.names.empty() && !route.names.contains(widget.value("name", ""))) {
        return false;
    }
    return !route.sharedOnly || IsSharedOotRow(widget);
}

using FleetRows = void (FleetSaveMenu::*)();
struct FleetSection {
    const char* heading;
    FleetRows draw;
    size_t rows;
};
struct RandomizerPageLayout {
    const char* label;
    FleetRows fleetRows;
    std::vector<const char*> headings;
    std::vector<FleetSection> fleetSections = {};
};
const std::vector<RandomizerPageLayout>& RandomizerPages() {
    static const std::vector<RandomizerPageLayout> pages = {
        { "General", &FleetSaveMenu::DrawGeneralPage, { "Enhancements", "" } },
        { "Logic & Access",
          nullptr,
          { "Logic", "Dungeon Items", "Key Rings", "Master Quest", "Win Gates", "Access", "Souls" },
          { { "Logic", &FleetSaveMenu::DrawSongPlacement, 2 },
            { "Dungeon Items", &FleetSaveMenu::DrawDungeonItemRows, 12 },
            { "Key Rings", &FleetSaveMenu::DrawKeyRingCount, 1 } } },
        { "Shuffles",
          nullptr,
          { "Cross-Game Entrances", "Entrances", "Swords & Shields", "Shuffle Items", "Shops & Merchants", "Check Pool",
            "Extra Items", "Keys", "Master Quest" },
          { { "Cross-Game Entrances", &FleetSaveMenu::DrawCrossEntranceRows, 11 },
            { "Swords & Shields", &FleetSaveMenu::DrawStartingEquipment, 3 } } },
        { "Item Pool", &FleetSaveMenu::DrawItemPoolPage, { "Item Pool", "Additional Items", "Traps" } },
        { "Enemies",
          nullptr,
          { "Options", "Ocarina of Time", "Majora's Mask" },
          { { "Options", &FleetSaveMenu::DrawBossEntranceRow, 1 } } },
        { "Plandomizer", &FleetSaveMenu::DrawPlandomizerPage, {} },
        { "Hints", nullptr, { "Hints", "Static Hints" } },
        { "Tricks/Glitches", nullptr, { "" } },
        { "Mods", nullptr, { "" } },
    };
    return pages;
}

constexpr const char* kRandomizerPage = "fleet:randomizer";
constexpr const char* kGiveItemsPage = "fleet:give";
constexpr const char* kAnchorPage = "fleet:anchor";
constexpr const char* kAnchorSidebar = "Anchor";
const std::set<std::string> kAnchorRoomRows = { "AnchorRoomMenu", "AnchorAdminMenu" };

const std::set<std::string> kSharedDevToolsPages = { "General", "Collision Viewer", "Save Editor" };
} // namespace

class FleetGui::Menu : public Ship::GuiWindow {
  public:
    Menu() : GuiWindow("", false, "Fleet") {
    }
    std::map<std::string, std::shared_ptr<Ship::GuiWindow>> children;
    std::map<std::string, unsigned> draws;
    std::map<std::string, float> pageHeights;
    std::map<std::string, nlohmann::json> models;
    std::map<std::pair<std::string, std::string>, nlohmann::json> placedRows;
    std::set<std::string> hiddenMmRows;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> sidebars;
    std::string selected = "oot";
    std::string scope = "settings";
    std::array<char, 256> search{};
    std::shared_ptr<std::string> loading;
    std::shared_ptr<FleetSaveMenu> generator;
    std::function<void()> reset;
    std::function<std::string(const std::string&, const std::string&, const std::string&)> request;
    FleetGui::ItemTools itemTools;
    FleetGui::Items givable;
    std::array<char, 64> giveFilter{};
    std::string giveStatus;
    struct Entry {
        std::string group, label, game, section;
        nlohmann::json sidebar;
        std::string Key() const {
            return group + "/" + label;
        }
    };
    void InitElement() override {
    }
    void UpdateElement() override {
    }
    void DrawElement() override {
    }
    void LoadModels() {
        if (!request) {
            return;
        }
        bool isAnyLoaded = false;
        for (const auto& [game, child] : children) {
            if (models[game].is_array()) {
                continue;
            }
            models[game] = nlohmann::json::parse(request(game, "MenuModel", "{}"), nullptr, false);
            isAnyLoaded |= models[game].is_array();
        }
        if (!isAnyLoaded) {
            return;
        }
        MarkOotOptionGroups(models["oot"]);
        ForceRowsOn();
        if (models["mm"].is_array()) {
            HideSharedRandoOptions(models["mm"]);
        }
        PlaceRandomizerRows();
    }
    static void ForceRowsOn() {
        const auto oot = Ship::GameRegistry::Get("oot");
        if (!oot) {
            return;
        }
        for (const auto& cvar : kRowsForcedOn) {
            oot->ConsoleVariables->SetInteger(cvar.c_str(), 1);
        }
    }
    void HideSharedRandoOptions(nlohmann::json& model) {
        const auto ootCvars = ModelCvars(models["oot"]);
        hiddenMmRows.clear();
        for (const auto& pair : SettingsSync::RandoPairs()) {
            if (!IsOotModOption(pair.oot) || ootCvars.contains(pair.oot)) {
                hiddenMmRows.insert(pair.mm);
            }
        }
        const auto isOwn = [this](const nlohmann::json& widget) {
            return !hiddenMmRows.contains(widget.value("cvar", ""));
        };
        for (auto& section : model) {
            for (auto& side : section.at("sidebars")) {
                side = KeptWidgets(std::move(side), isOwn);
            }
        }
        auto hidden = nlohmann::json(hiddenMmRows);
        for (const auto& owned : kMmRowsOwnedByFleet) {
            hidden.push_back(owned);
        }
        request("mm", "HideRandoOptions", hidden.dump());
    }
    static std::set<std::string> ModelCvars(const nlohmann::json& model) {
        std::set<std::string> cvars;
        if (!model.is_array()) {
            return cvars;
        }
        for (const auto& section : model) {
            for (const auto& side : section.at("sidebars")) {
                for (const auto& widget : side.at("widgets")) {
                    cvars.insert(widget.value("cvar", ""));
                }
            }
        }
        return cvars;
    }
    static void MarkOotOptionGroups(nlohmann::json& model) {
        if (!model.is_array()) {
            return;
        }
        for (auto& section : model) {
            if (section.at("name") != "Randomizer") {
                continue;
            }
            for (auto& side : section.at("sidebars")) {
                MarkSidebarOptionGroups(side);
            }
        }
    }
    static void MarkSidebarOptionGroups(nlohmann::json& side) {
        std::string group;
        int column = -1;
        for (auto& widget : side.at("widgets")) {
            if (widget.at("column").get<int>() != column) {
                column = widget.at("column").get<int>();
                group.clear();
            }
            const std::string name = widget.value("name", "");
            const bool isSeparator = widget.value("cvar", "").empty() && kOotOptionGroups.contains(name);
            if (isSeparator) {
                group = name;
            }
            widget["group"] = group;
            widget["separator"] = isSeparator;
        }
    }
    const RowRoute* FindRoute(const std::string& game, const std::string& section, const std::string& sidebar,
                              const nlohmann::json& widget) const {
        if (widget.value("separator", false) || (game == "mm" && IsMmFrameWidget(widget)) ||
            IsDrawnByFleet(widget.value("cvar", ""))) {
            return nullptr;
        }
        for (const auto& route : RowRoutes()) {
            if (IsRouteFor(route, game, section, sidebar, widget)) {
                return &route;
            }
        }
        return nullptr;
    }
    void PlaceRandomizerRows() {
        placedRows.clear();
        for (const char* game : { "oot", "mm" }) {
            const auto& model = models[game];
            if (!model.is_array()) {
                continue;
            }
            for (const auto& section : model) {
                for (const auto& side : section.at("sidebars")) {
                    PlaceSidebarRows(game, section.at("name").get<std::string>(), side);
                }
            }
        }
    }
    void PlaceSidebarRows(const std::string& game, const std::string& section, const nlohmann::json& side) {
        const std::string sidebar = side.at("name");
        std::map<std::pair<std::string, std::string>, nlohmann::json> parts;
        for (const auto& widget : side.at("widgets")) {
            const RowRoute* route = FindRoute(game, section, sidebar, widget);
            if (route == nullptr) {
                continue;
            }
            auto row = widget;
            row["slot"] = 0;
            for (const auto& place : route->places) {
                auto& part = parts[{ place.page, place.heading }];
                if (part.is_null()) {
                    const nlohmann::json rows = { { "name", sidebar },
                                                  { "columns", 1 },
                                                  { "widgets", nlohmann::json::array() } };
                    part = nlohmann::json{ { "game", game },
                                           { "section", section },
                                           { "label", sidebar },
                                           { "heading", place.heading },
                                           { "sidebar", rows } };
                }
                part["sidebar"]["widgets"].push_back(row);
            }
        }
        for (auto& [place, part] : parts) {
            placedRows[place].push_back(std::move(part));
        }
    }
    struct PageSection {
        std::string heading;
        FleetRows fleetRows = nullptr;
        std::vector<nlohmann::json> parts;
        size_t rows = 0;
    };
    std::vector<PageSection> ComposeSections(const RandomizerPageLayout& layout) {
        std::vector<PageSection> sections;
        for (const char* heading : layout.headings) {
            PageSection section{ heading };
            for (const auto& fleetSection : layout.fleetSections) {
                if (std::string_view(fleetSection.heading) == heading) {
                    section.fleetRows = fleetSection.draw;
                    section.rows += fleetSection.rows;
                }
            }
            if (const auto placed = placedRows.find({ layout.label, heading }); placed != placedRows.end()) {
                for (const auto& part : placed->second) {
                    section.rows += part.at("sidebar").at("widgets").size();
                    section.parts.push_back(part);
                }
            }
            if (section.fleetRows != nullptr || !section.parts.empty()) {
                sections.push_back(std::move(section));
            }
        }
        return sections;
    }
    void DrawRandomizerPage(const Entry& entry) {
        const auto layout = std::find_if(RandomizerPages().begin(), RandomizerPages().end(),
                                         [&](const auto& page) { return entry.label == page.label; });
        if (layout == RandomizerPages().end()) {
            return;
        }
        if (generator) {
            generator->WarmPoolWorker();
        }
        const float before = ImGui::GetCursorPosY();
        if (layout->fleetRows != nullptr) {
            DrawFleetRows(layout->fleetRows);
        }
        DrawSectionsInColumns(entry, ComposeSections(*layout));
        pageHeights["page/" + entry.label] = ImGui::GetCursorPosY() - before;
    }
    static std::vector<size_t> FirstSectionOfEachColumn(const std::vector<PageSection>& sections, size_t columns) {
        size_t total = 0;
        for (const auto& section : sections) {
            total += section.rows;
        }
        std::vector<size_t> starts = { 0 };
        size_t filled = 0;
        for (size_t index = 0; index + 1 < sections.size() && starts.size() < columns; index++) {
            filled += sections[index].rows;
            if (filled * columns >= total * starts.size()) {
                starts.push_back(index + 1);
            }
        }
        return starts;
    }
    static void SplitRowsInTwo(PageSection& section) {
        for (auto& part : section.parts) {
            auto& widgets = part["sidebar"]["widgets"];
            const size_t count = widgets.size();
            for (size_t index = 0; index < count; index++) {
                widgets[index]["slot"] = index * 2 / count;
            }
            part["sidebar"]["columns"] = 2;
        }
    }
    void DrawSection(const Entry& entry, const PageSection& section) {
        if (!section.heading.empty()) {
            ImGui::SeparatorText(section.heading.c_str());
        }
        if (section.fleetRows != nullptr) {
            ImGui::PushID("fleet");
            DrawFleetRows(section.fleetRows);
            ImGui::PopID();
        }
        for (const auto& part : section.parts) {
            ImGui::PushID(part.value("label", entry.label).c_str());
            ImGui::PushID(part.at("game").get<std::string>().c_str());
            Sidebar({ "", part.value("label", entry.label), part.at("game"), part.at("section"), part.at("sidebar") });
            ImGui::PopID();
            ImGui::PopID();
        }
    }
    void DrawSectionsInColumns(const Entry& entry, std::vector<PageSection> sections) {
        constexpr size_t kMaxColumns = 3;
        constexpr size_t kRowsWorthSplitting = 12;
        constexpr float kMinColumnWidth = 380.0f;
        const float available = ImGui::GetContentRegionAvail().x;
        const size_t fitting = std::max<size_t>(1, static_cast<size_t>(available / kMinColumnWidth));
        if (sections.size() == 1 && sections.front().rows >= kRowsWorthSplitting && fitting >= 2) {
            SplitRowsInTwo(sections.front());
        }
        const size_t columns = std::min({ kMaxColumns, fitting, sections.size() });
        if (columns < 2) {
            for (size_t index = 0; index < sections.size(); index++) {
                ImGui::PushID(static_cast<int>(index));
                DrawSection(entry, sections[index]);
                ImGui::PopID();
            }
            return;
        }
        const std::vector<size_t> starts = FirstSectionOfEachColumn(sections, columns);
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float width = (available - spacing * (starts.size() - 1)) / starts.size();
        for (size_t column = 0; column < starts.size(); column++) {
            const size_t end = column + 1 < starts.size() ? starts[column + 1] : sections.size();
            ImGui::PushID(static_cast<int>(column));
            ImGui::BeginChild("##SectionColumn", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY,
                              ImGuiWindowFlags_NoScrollbar);
            for (size_t index = starts[column]; index < end; index++) {
                ImGui::PushID(static_cast<int>(index));
                DrawSection(entry, sections[index]);
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::PopID();
            if (column + 1 < starts.size()) {
                ImGui::SameLine();
            }
        }
    }
    void DrawFleetRows(FleetRows fleetRows) {
        if (!generator) {
            ImGui::TextDisabled("Fleet's own options are available on the file-select screen.");
            return;
        }
        (generator.get()->*fleetRows)();
    }
    void Widget(const Entry& entry, const nlohmann::json& widget) {
        if (!request || IsHiddenByCurrentMode(widget.value("cvar", ""))) {
            return;
        }
        const std::string key = entry.game + entry.section + entry.label;
        ImGui::PushID(key.c_str());
        ImGui::PushID(widget.at("index").get<int>());
        const bool isForcedOn = kRowsForcedOn.contains(widget.value("cvar", ""));
        ImGui::BeginDisabled(isForcedOn);
        request(entry.game, "MenuDrawWidget",
                nlohmann::json{ { "section", entry.section },
                                { "sidebar", entry.sidebar.at("name") },
                                { "column", widget.at("column") },
                                { "index", widget.at("index") } }
                    .dump());
        ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::PopID();
        ++draws[entry.game];
    }
    void Sidebar(const Entry& entry) {
        if (entry.sidebar.contains("paired")) {
            int partIndex = 0;
            std::string previousHeading;
            for (const auto& part : entry.sidebar.at("paired")) {
                const std::string heading = part.value("heading", "");
                ImGui::PushID(partIndex++);
                if (!heading.empty() && heading != previousHeading) {
                    ImGui::SeparatorText(heading.c_str());
                }
                previousHeading = heading;
                Sidebar(
                    { "", part.value("label", entry.label), part.at("game"), part.at("section"), part.at("sidebar") });
                ImGui::PopID();
            }
            return;
        }
        const float before = ImGui::GetCursorPosY();
        if (scope == entry.game) {
            NativePage(entry);
        } else {
            Columns(entry);
        }
        pageHeights[entry.game + "/" + entry.label] = ImGui::GetCursorPosY() - before;
        pageHeights[selected + "|" + entry.game + "/" + entry.label] = ImGui::GetCursorPosY() - before;
    }
    void GiveItems() {
        if (!itemTools.list || !itemTools.give) {
            return;
        }
        ComboRando::ComboMenu_PushButton(ComboRando::ComboMenu_ThemeColor());
        if (ImGui::Button("Give All Items") && itemTools.giveAll) {
            giveStatus = itemTools.giveAll();
        }
        ImGui::SameLine();
        if (ImGui::Button("Draw-Test All Items") && itemTools.drawTest) {
            giveStatus = itemTools.drawTest();
        }
        ComboRando::ComboMenu_PopButton();
        ImGui::TextDisabled("Give All Items hands over every item of both games. Draw-Test loads the icon and model "
                            "of each one and logs it first, so a crash names the culprit.");
        if (givable.empty()) {
            givable = itemTools.list();
        }
        if (givable.empty()) {
            ImGui::TextDisabled("Open a Fleet save to give its items.");
            return;
        }
        ImGui::TextWrapped("Click an item to receive it now; click again for the next level of a progressive one. When "
                           "both games count it (a shared item or a mod pair), the other game receives its copy too.");
        ImGui::SetNextItemWidth(260);
        ImGui::InputTextWithHint("##giveFilter", "Search items...", giveFilter.data(), giveFilter.size());
        if (!giveStatus.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", giveStatus.c_str());
        }
        const std::string filter = ToLowerCase(giveFilter.data());
        const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        for (const char* game : { "oot", "mm" }) {
            if (std::string(game) == "mm") {
                ImGui::SameLine();
            }
            ImGui::BeginChild(game, ImVec2(width, 0), ImGuiChildFlags_Borders);
            ImGui::TextDisabled("%s", GameTitle(game));
            GivableList(game, filter);
            ImGui::EndChild();
        }
    }
    static std::string ToLowerCase(std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
        return text;
    }
    void GivableRow(const FleetGui::GivableItem& item) {
        constexpr float kIconSize = 24.0f;
        FleetItemIcon icon = {};
        if (itemTools.findIcon && itemTools.findIcon(item.game, item.name, icon)) {
            ImGui::ImageWithBg(reinterpret_cast<ImTextureID>(reinterpret_cast<uintptr_t>(icon.texture)),
                               ImVec2(kIconSize, kIconSize), ImVec2(icon.uv0[0], icon.uv0[1]),
                               ImVec2(icon.uv1[0], icon.uv1[1]), ImVec4(0, 0, 0, 0),
                               ImVec4(icon.tint[0], icon.tint[1], icon.tint[2], icon.tint[3]));
        } else {
            ImGui::Dummy(ImVec2(kIconSize, kIconSize));
        }
        ImGui::SameLine();
        ImGui::PushID(item.name.c_str());
        if (ImGui::Selectable(item.label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(0, kIconSize))) {
            const std::string shown = item.label + " (" + GameTitle(item.game) + ")";
            giveStatus = itemTools.give(item.game, item.name) ? "Gave " + shown : "Could not give " + shown;
        }
        if (item.label != item.name) {
            ImGui::SetItemTooltip("%s", item.name.c_str());
        }
        ImGui::PopID();
    }
    void GivableList(const std::string& game, const std::string& filter) {
        std::vector<const FleetGui::GivableItem*> shown;
        for (const auto& item : givable) {
            if (item.game == game && ToLowerCase(item.label + " " + item.name).find(filter) != std::string::npos) {
                shown.push_back(&item);
            }
        }
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(shown.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
                GivableRow(*shown[row]);
            }
        }
        ImGui::PopStyleVar();
    }
    const nlohmann::json* FindSidebar(const std::string& game, const std::string& section, const std::string& sidebar) {
        const auto model = models.find(game);
        if (model == models.end() || !model->second.is_array()) {
            return nullptr;
        }
        for (const auto& candidate : model->second) {
            if (candidate.at("name").get<std::string>() != section) {
                continue;
            }
            for (const auto& side : candidate.at("sidebars")) {
                if (side.at("name").get<std::string>() == sidebar) {
                    return &side;
                }
            }
        }
        return nullptr;
    }
    nlohmann::json AnchorPart(const std::string& game, const nlohmann::json& side, const Rows& rows,
                              const std::string& heading) {
        return {
            { "game", game }, { "section", "Network" }, { "sidebar", KeptRows(side, rows) }, { "heading", heading }
        };
    }
    bool AddAnchorPage(std::vector<Entry>& entries) {
        const nlohmann::json* oot = FindSidebar("oot", "Network", kAnchorSidebar);
        if (oot == nullptr) {
            return false;
        }
        auto parts = nlohmann::json::array();
        parts.push_back(AnchorPart("oot", *oot, Except(kAnchorRoomRows), ""));
        parts.push_back(AnchorPart("oot", *oot, Only(kAnchorRoomRows), "Ocarina of Time room"));
        const nlohmann::json* mm = FindSidebar("mm", "Network", kAnchorSidebar);
        if (mm != nullptr) {
            parts.push_back(AnchorPart("mm", *mm, Only(kAnchorRoomRows), "Majora's Mask room"));
        }
        entries.push_back({ kBothGames, kAnchorSidebar, kAnchorPage, "", { { "paired", std::move(parts) } } });
        return true;
    }
    void NativePage(const Entry& entry) {
        if (!request) {
            return;
        }
        ImGui::PushID((entry.game + entry.section + entry.label).c_str());
        request(entry.game, "MenuDrawPage",
                nlohmann::json{ { "section", entry.section }, { "sidebar", entry.sidebar.at("name") } }.dump());
        ImGui::PopID();
        ++draws[entry.game];
    }
    static size_t LayoutColumn(const nlohmann::json& widget) {
        return widget.value("slot", widget.at("column").get<size_t>());
    }
    void Columns(const Entry& entry) {
        const auto& widgets = entry.sidebar.at("widgets");
        std::set<size_t> usedColumns;
        for (const auto& widget : widgets) {
            usedColumns.insert(LayoutColumn(widget));
        }
        if (usedColumns.size() < 2 || ImGui::GetContentRegionAvail().x < 800) {
            for (const auto& widget : widgets) {
                Widget(entry, widget);
            }
            return;
        }
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const size_t count = usedColumns.size();
        const float width = (ImGui::GetContentRegionAvail().x - spacing * (count - 1)) / count;
        const size_t lastColumn = *usedColumns.rbegin();
        for (const size_t column : usedColumns) {
            ImGui::PushID(static_cast<int>(column));
            ImGui::BeginChild("##Column", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
            for (const auto& widget : widgets) {
                if (LayoutColumn(widget) == column) {
                    Widget(entry, widget);
                }
            }
            ImGui::EndChild();
            ImGui::PopID();
            if (column != lastColumn) {
                ImGui::SameLine();
            }
        }
    }
    void Search() {
        const auto query = ToLowerCase(search.data());
        std::set<std::string> seen;
        for (const auto* gameName : { "oot", "mm" }) {
            const std::string game(gameName);
            const auto& model = models[game];
            if (!model.is_array()) {
                continue;
            }
            for (const auto& section : model) {
                for (const auto& side : section.at("sidebars")) {
                    Entry entry{ "", side.at("name"), game, section.at("name"), side };
                    for (const auto& widget : side.at("widgets")) {
                        const auto text = ToLowerCase(widget.value("name", "") + " " + widget.value("tooltip", ""));
                        if (!widget.value("search", false) || text.find(query) == std::string::npos) {
                            continue;
                        }
                        const RowRoute* route = FindRoute(game, entry.section, entry.label, widget);
                        const auto key = widget.value("cvar", "") + "|" + widget.value("name", "");
                        if ((route == nullptr && IsRandomizerSection(entry.section)) || !seen.insert(key).second) {
                            continue;
                        }
                        SearchResult(entry, widget, route);
                    }
                }
            }
        }
    }
    void SearchResult(const Entry& entry, const nlohmann::json& widget, const RowRoute* route) {
        Widget(entry, widget);
        if (route == nullptr) {
            ImGui::TextDisabled("%s > %s > %s", entry.game.c_str(), entry.section.c_str(), entry.label.c_str());
        } else {
            ImGui::TextDisabled("Randomizer > %s", route->places.front().page);
        }
        ImGui::Separator();
    }
    bool Shown(const std::string& game, const std::string& section, const std::string& label) {
        if (section == "Settings" || section == "Network" || IsRandomizerSection(section)) {
            return false;
        }
        if (section == "Dev Tools") {
            return !kSharedDevToolsPages.contains(label);
        }
        return true;
    }
    void Panel(const std::vector<Entry>& entries) {
        if (entries.empty()) {
            ImGui::TextUnformatted("Menu not available yet (game still initializing).");
            return;
        }
        auto& active = sidebars[scope];
        auto chosen =
            std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.Key() == active; });
        if (chosen == entries.end()) {
            active = entries.front().Key();
        }
        float need = 0;
        for (const auto& entry : entries) {
            need = std::max(need, ImGui::CalcTextSize(entry.label.c_str()).x);
            need = std::max(need, ImGui::CalcTextSize(entry.group.c_str()).x);
        }
        const float available = ImGui::GetContentRegionAvail().x;
        const float nativeWidth = available > 1600 ? available * 0.15f : 200.0f - ImGui::GetStyle().ItemSpacing.x;
        const bool nativeScope = scope == "oot" || scope == "mm";
        const float width =
            nativeScope
                ? nativeWidth
                : std::min(available * 0.5f, std::max(nativeWidth, need * 1.2f + 28 + ImGui::GetStyle().ScrollbarSize));
        ImGui::BeginChild("##HubSidebar", ImVec2(width, 0), 0);
        ImGui::SetWindowFontScale(1.2f);
        std::string group;
        for (const auto& entry : entries) {
            if (group != entry.group) {
                group = entry.group;
                if (!group.empty()) {
                    ImGui::SeparatorText(group.c_str());
                }
            }
            ImGui::PushID(entry.Key().c_str());
            if (ComboRando::ComboMenu_StyledSidebarEntry(entry.label.c_str(), entry.Key() == active,
                                                         ImGui::GetContentRegionAvail().x)) {
                active = entry.Key();
                search.fill(0);
            }
            ImGui::PopID();
        }
        ImGui::SetWindowFontScale(1);
        ImGui::EndChild();
        ImGui::SameLine();
        const auto p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 4, p.y + ImGui::GetContentRegionAvail().y),
                                                  IM_COL32(255, 255, 255, 255));
        ImGui::Dummy(ImVec2(4, 0));
        ImGui::SameLine();
        ImGui::BeginChild("##HubContent", ImVec2(0, 0));
        chosen = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.Key() == active; });
        if (search[0]) {
            Search();
        } else if (chosen != entries.end() && chosen->game == kGiveItemsPage) {
            GiveItems();
        } else if (chosen != entries.end() && chosen->game == kRandomizerPage) {
            DrawRandomizerPage(*chosen);
        } else if (chosen != entries.end()) {
            Sidebar(*chosen);
        }
        ImGui::EndChild();
    }
    bool AddToSettingsPages(std::vector<Entry>& entries, const std::string& game, const std::string& section,
                            const nlohmann::json& side) {
        bool claimed = false;
        for (const auto& page : SettingsPages()) {
            for (size_t index = 0; index < page.parts.size(); index++) {
                const PagePart& part = page.parts[index];
                if (game != part.game || section != part.section || side.at("name") != part.sidebar) {
                    continue;
                }
                claimed = true;
                if (part.rows.Absorbed()) {
                    continue;
                }
                auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& candidate) {
                    return candidate.group == page.group && candidate.label == page.label;
                });
                entry->sidebar["paired"].push_back({ { "game", game },
                                                     { "section", section },
                                                     { "sidebar", KeptRows(side, part.rows) },
                                                     { "heading", part.heading != nullptr ? part.heading : "" },
                                                     { "order", index } });
            }
        }
        return claimed;
    }
    void Content() {
        std::vector<Entry> entries;
        const bool native = scope == "oot" || scope == "mm";
        if (native && models[scope].is_array()) {
            auto& header = headers[scope];
            bool first = true;
            for (const auto& section : models[scope]) {
                const std::string name = section.at("name");
                bool shown = false;
                for (const auto& side : section.at("sidebars")) {
                    shown |= Shown(scope, name, side.at("name"));
                }
                if (!shown) {
                    continue;
                }
                if (header.empty()) {
                    header = name;
                }
                if (!first) {
                    ImGui::SameLine();
                }
                first = false;
                if (ComboRando::ComboMenu_StyledHeaderEntry(name.c_str(), header == name)) {
                    header = name;
                    sidebars[scope].clear();
                    search.fill(0);
                }
            }
            ImGui::Separator();
        }
        if (scope == "settings") {
            for (const auto& page : SettingsPages()) {
                entries.push_back({ page.group, page.label, "paired", "", { { "paired", nlohmann::json::array() } } });
            }
        }
        if (scope == "devtools") {
            entries.push_back({ "Fleet", "Give Items", kGiveItemsPage, "", {} });
        }
        if (scope == "randomizer") {
            for (const auto& layout : RandomizerPages()) {
                entries.push_back({ "Randomizer", layout.label, kRandomizerPage, "", {} });
            }
        }
        const bool hasAnchorPage = scope == "network" && AddAnchorPage(entries);
        for (const auto* gameName : { "oot", "mm" }) {
            const std::string game(gameName);
            const auto& model = models[game];
            if (!model.is_array() || (native && game != scope) || scope == "randomizer") {
                continue;
            }
            for (const auto& section : model) {
                const std::string name = section.at("name");
                const bool rando = IsRandomizerSection(name);
                for (const auto& side : section.at("sidebars")) {
                    const std::string label = side.at("name");
                    if (scope == "devtools" && name == "Dev Tools" && kSharedDevToolsPages.contains(label)) {
                        auto existing = std::find_if(entries.begin(), entries.end(),
                                                     [&](const auto& entry) { return entry.label == label; });
                        if (existing == entries.end()) {
                            entries.push_back(
                                { "Shared", label, "paired", "", { { "paired", nlohmann::json::array() } } });
                            existing = std::prev(entries.end());
                        }
                        existing->sidebar["paired"].push_back({ { "game", game },
                                                                { "section", name },
                                                                { "sidebar", side },
                                                                { "heading", GameTitle(game) } });
                        continue;
                    }
                    if (!native && scope == "settings" && rando && label.find("Tracker") != std::string::npos) {
                        auto existing = std::find_if(entries.begin(), entries.end(),
                                                     [&](const auto& entry) { return entry.label == label; });
                        if (existing == entries.end()) {
                            entries.push_back(
                                { "Trackers", label, "paired", "", { { "paired", nlohmann::json::array() } } });
                            existing = std::prev(entries.end());
                        }
                        existing->sidebar["paired"].push_back({ { "game", game },
                                                                { "section", name },
                                                                { "sidebar", side },
                                                                { "heading", GameTitle(game) } });
                        continue;
                    }
                    if (!native && scope == "settings" && AddToSettingsPages(entries, game, name, side)) {
                        continue;
                    }
                    if (native) {
                        if (name == headers[scope] && Shown(game, name, label)) {
                            entries.push_back({ "", label, game, name, side });
                        }
                    } else if (scope == "network" && name == "Network") {
                        if (hasAnchorPage && label == kAnchorSidebar) {
                            continue;
                        }
                        const std::string shown =
                            label == "NEI-Store" ? (game == "oot" ? "OoT NEI-Store" : "MM NEI-Store") : label;
                        entries.push_back({ GameTitle(game), shown, game, name, side });
                    } else if (scope == "settings" && name == "Settings") {
                        entries.push_back({ GameTitle(game), label, game, name, side });
                    }
                }
            }
        }
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [](const Entry& entry) {
                                         return entry.game == "paired" && entry.sidebar.at("paired").empty();
                                     }),
                      entries.end());
        for (auto& entry : entries) {
            if (entry.game == "paired") {
                auto& parts = entry.sidebar.at("paired");
                std::stable_sort(parts.begin(), parts.end(), [](const auto& a, const auto& b) {
                    return a.value("order", 0) < b.value("order", 0);
                });
            }
        }
        Panel(entries);
    }
    void Draw() override {
        ImGui::SetCurrentContext(Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetImGuiContext());
        const bool isLoading = loading && !loading->empty();
        if (isLoading || !IsVisible()) {
            return;
        }
        // validate-native-name.ps1 waits for this line.
        static bool traced = false;
        if (!traced) {
            std::cout << "[fleet] unified menu drawing: " << scope << "/" << sidebars[scope] << std::endl;
            traced = true;
        }
        LoadModels();
        const auto* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,
                              ImVec4(0, 0, 0, CVarGetFloat("gSettings.Menu.BackgroundOpacity", 0.85f)));
        const bool open =
            ImGui::Begin("Combo Menu", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (open) {
            const float ww = vp->WorkSize.x;
            const float wh = vp->WorkSize.y;
            const ImVec2 size(ww > 1280 ? std::min(ww * 0.9f, wh * 1.77f) : ww, wh > 800 ? wh * 0.9f : wh);
            ImGui::SetCursorScreenPos(
                ImVec2(vp->WorkPos.x + (ww - size.x) * 0.5f, vp->WorkPos.y + (wh - size.y) * 0.5f));
            ImGui::BeginChild("##ComboMenuBlock", size, 0, ImGuiWindowFlags_NoScrollbar);
            ImGui::PushID("##ComboScopeTabs");
            const std::array<std::pair<const char*, const char*>, 6> scopes{ { { "settings", "Settings" },
                                                                               { "randomizer", "Randomizer" },
                                                                               { "network", "Network" },
                                                                               { "devtools", "Developer Tools" },
                                                                               { "oot", "Ocarina of Time" },
                                                                               { "mm", "Majora's Mask" } } };
            for (const auto& [id, label] : scopes) {
                if (std::string(id) != "settings") {
                    ImGui::SameLine();
                }
                if (ComboRando::ComboMenu_StyledHeaderEntry(label, scope == id)) {
                    scope = id;
                    search.fill(0);
                }
            }
            ImGui::PopID();
            if (ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x - ImGui::GetItemRectMax().x > 330) {
                ImGui::SameLine();
            }
            ImGui::SetNextItemWidth(200);
            ImGui::InputTextWithHint("##ComboSearch", "Search...", search.data(), search.size());
            ComboRando::ComboMenu_PushButton(ImVec4(0.55f, 0, 0, 1));
            const auto& style = ImGui::GetStyle();
            const float total = ImGui::CalcTextSize(ICON_FA_POWER_OFF ICON_FA_UNDO ICON_FA_TIMES_CIRCLE).x +
                                style.FramePadding.x * 6 + style.ItemSpacing.x * 2;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - total);
            if (ImGui::Button(ICON_FA_POWER_OFF)) {
                Ship::Context::GetRawInstance()->GetWindow()->Close();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Quit");
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_UNDO) && reset) {
                Hide();
                reset();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reset (Ctrl+R)");
            }
            ComboRando::ComboMenu_PopButton();
            ImGui::SameLine();
            ComboRando::ComboMenu_PushButton(ImVec4(0.45f, 0.45f, 0.45f, 1));
            if (ImGui::Button(ICON_FA_TIMES_CIRCLE)) {
                Hide();
            }
            ComboRando::ComboMenu_PopButton();
            const auto line = ImGui::GetCursorScreenPos();
            const float width = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddRectFilled(line, ImVec2(line.x + width, line.y + 4), IM_COL32_WHITE);
            ImGui::Dummy(ImVec2(width, 4 + style.ItemSpacing.y));
            Content();
            ImGui::EndChild();
        }
        ImGui::End();
    }

  protected:
    void SetVisibility(bool visible) override {
        if (!visible && generator && !generator->CloseConfiguration()) {
            return;
        }
        Ship::GuiElement::SetVisibility(visible);
        if (visible) {
            models.clear();
            placedRows.clear();
            givable.clear();
        }
        if (!visible) {
            for (auto& [game, child] : children) {
                Ship::GameScope owner(game);
                child->Hide();
                if (const auto services = Ship::GameRegistry::Get(game)) {
                    services->ConsoleVariables->Save();
                }
            }
        }
    }
};

FleetGui::FleetGui() : mFleetMenu(std::make_shared<Menu>()) {
    mFleetMenu->loading = mLoadingText;
    mLoadingOverlay = std::make_shared<LoadingOverlay>(mLoadingText);
    AddGuiWindow(mLoadingOverlay);
    mReviewOverlay = std::make_shared<ReviewOverlay>(mReviewPrompt);
    AddGuiWindow(mReviewOverlay);
    mDefaults = mGuiWindows;
}

void FleetGui::SetNativeRequest(
    std::function<std::string(const std::string&, const std::string&, const std::string&)> request) {
    mFleetMenu->request = [this, request = std::move(request)](const auto& game, const auto& op, const auto& payload) {
        if (op != "MenuDrawWidget" || game == mFleetMenu->selected || !mChildren.contains(game)) {
            return request(game, op, payload);
        }
        auto foreground = std::move(mGuiWindows);
        mGuiWindows = mChildren.at(game).windows;
        try {
            auto result = request(game, op, payload);
            mChildren.at(game).windows = mGuiWindows;
            mGuiWindows = std::move(foreground);
            return result;
        } catch (...) {
            mGuiWindows = std::move(foreground);
            throw;
        }
    };
}
void FleetGui::SetLoading(const std::string& text) {
    *mLoadingText = text;
    if (text.empty()) {
        mLoadingOverlay->Hide();
    } else {
        mLoadingOverlay->Show();
    }
}
void FleetGui::SetReviewPrompt(const std::string& game, const std::string& item, const std::string& text) {
    mReviewPrompt->game = game;
    mReviewPrompt->item = item;
    mReviewPrompt->text = text;
    if (text.empty()) {
        mReviewOverlay->Hide();
    } else {
        mReviewOverlay->Show();
    }
}
void FleetGui::SetGenerator(std::shared_ptr<FleetSaveMenu> generator) {
    mFleetMenu->generator = std::move(generator);
}

void FleetGui::SetItemTools(ItemTools tools) {
    mReviewPrompt->findIcon = tools.findIcon;
    mFleetMenu->itemTools = std::move(tools);
}
void FleetGui::OpenRandomizer() {
    mFleetMenu->scope = "randomizer";
    mFleetMenu->sidebars["randomizer"] = "Randomizer/General";
    mFleetMenu->Show();
}
int FleetGui::ChooseGame(int current, int automation) {
    const char* labels[] = { "Combo (OoT + MM)", "Ocarina of Time", "Majora's Mask" };
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    while (window->IsRunning()) {
        window->HandleEvents();
        if (!window->IsFrameReady()) {
            continue;
        }
        ImGui::SetCurrentContext(GetImGuiContext());
        StartDraw();
        window->StartFrame();
        window->RunGuiOnly();
        const auto* vp = ImGui::GetMainViewport();
        const float width = std::max(220.0f, std::min(520.0f, vp->WorkSize.x - 32));
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0), ImVec2(width, vp->WorkSize.y - 32));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.09f, 0.11f, 0.98f));
        const bool open = ImGui::Begin("Fleet - Choose Game", nullptr,
                                       ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        int result = -2;
        if (open) {
            const auto theme = ComboRando::ComboMenu_ThemeColor();
            ImGui::SetWindowFontScale(1.4f);
            ImGui::TextColored(theme, "Choose your game");
            ImGui::SetWindowFontScale(1);
            ImGui::TextDisabled("Last played: %s", labels[std::clamp(current, 0, 2)]);
            ImGui::Separator();
            ImGui::TextWrapped("Play both games together, or play either game on its own. Solo games offer vanilla and "
                               "randomizer files.");
            ImGui::Spacing();
            ComboRando::ComboMenu_PushButton(theme);
            for (int mode = 0; mode < 3; mode++) {
                if (ImGui::Button(labels[mode], ImVec2(-1, 48))) {
                    result = mode;
                }
            }
            ImGui::Spacing();
            if (ImGui::Button("Quit", ImVec2(-1, 0))) {
                result = -1;
            }
            ComboRando::ComboMenu_PopButton();
        }
        ImGui::End();
        EndDraw();
        window->EndFrame();
        if (automation >= 0) {
            return automation;
        }
        if (result != -2) {
            return result;
        }
    }
    return -1;
}

void FleetGui::SetReset(std::function<void()> callback) {
    mFleetMenu->reset = std::move(callback);
}

bool FleetGui::ExerciseMenu(const std::string& game) {
    if (mFleetMenu->draws[game] > 0) {
        if (!GetGuiWindow("Configure Controller")) {
            throw std::runtime_error("Fleet has no shared controller editor");
        }
        if (mFleetMenu->pageHeights[game + "|oot/Controls"] < 200) {
            mFleetMenu->scope = "settings";
            mFleetMenu->sidebars["settings"] = "Settings/Controls";
            mFleetMenu->Show();
            return false;
        }
        mFleetMenu->Hide();
        return true;
    }
    mFleetMenu->selected = game;
    mFleetMenu->scope = game;
    mFleetMenu->Show();
    return false;
}

bool FleetGui::ExerciseRandomizerPages() {
    constexpr std::array<std::pair<const char*, const char*>, 5> kDrawnByPage = {
        { { "oot/Shuffles", "Shuffles" },
          { "mm/Check Pool", "Shuffles" },
          { "oot/Tricks/Glitches", "Tricks/Glitches" },
          { "page/Item Pool", "Item Pool" },
          { "page/Plandomizer", "Plandomizer" } }
    };
    for (const auto& [drawn, page] : kDrawnByPage) {
        if (mFleetMenu->pageHeights[drawn] <= 0) {
            mFleetMenu->scope = "randomizer";
            mFleetMenu->sidebars["randomizer"] = std::string("Randomizer/") + page;
            mFleetMenu->Show();
            return false;
        }
    }
    if (!GetGuiWindow("Configure Controller")) {
        throw std::runtime_error("Fleet has no shared controller editor");
    }
    if (mFleetMenu->pageHeights["oot/Controls"] < 200) {
        mFleetMenu->scope = "settings";
        mFleetMenu->sidebars["settings"] = "Settings/Controls";
        mFleetMenu->Show();
        return false;
    }
    mFleetMenu->Hide();
    return true;
}

void FleetGui::Capture(const std::string& game) {
    ImGui::SetCurrentContext(GetImGuiContext());
    if (game == "oot") {
        auto controller = GetGuiWindow("Configure Controller");
        if (controller && !std::dynamic_pointer_cast<OwnedWindow>(controller)) {
            controller = std::make_shared<OwnedWindow>("oot", controller);
            mDefaults["Configure Controller"] = controller;
            mGuiWindows["Configure Controller"] = controller;
            for (auto& [name, child] : mChildren) {
                child.windows["Configure Controller"] = controller;
            }
        }
    }
    if (GetMenu() != mFleetMenu && GetMenu() != nullptr) {
        mFleetMenu->children[game] = GetMenu();
        if (GetMenu()->IsVisible()) {
            mFleetMenu->Show();
        }
        SetMenu(mFleetMenu);
    }
    if (mFleetMenu->children.contains(game)) {
        if (mFleetMenu->children.at(game)->IsVisible() && !mFleetMenu->IsVisible()) {
            mFleetMenu->Show();
        }
        mChildren[game] = { mGuiWindows, mFleetMenu->children.at(game), GetMenuBar(), ImGui::GetStyle() };
    }
}

void FleetGui::Activate(const std::string& game) {
    ImGui::SetCurrentContext(GetImGuiContext());
    const auto child = mChildren.find(game);
    mFleetMenu->selected = game;
    if (child == mChildren.end()) {
        mGuiWindows = mDefaults;
        SetMenu(nullptr);
        SetMenuBar(nullptr);
        return;
    }
    mGuiWindows = child->second.windows;
    SetMenuBar(child->second.bar);
    SetMenu(mFleetMenu);
    ImGui::GetStyle() = child->second.style;
}
