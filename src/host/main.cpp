#include "AssetSetup.h"
#include "GameInstall.h"

#include <cctype>
#include <iomanip>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <csignal>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <SDL2/SDL.h>
#include <fast/Fast3dWindow.h>
#include <nlohmann/json.hpp>
#include <ship/Context.h>
#include <ship/window/FileDropMgr.h>
#include <ship/GameRegistry.h>
#include <libultraship/controller/controldeck/ControlDeck.h>
#include <ship/controller/controldevice/controller/Controller.h>
#include <ship/debug/Console.h>
#include <ship/debug/CrashHandler.h>

#include "GameFiber.h"
#include "GameModule.h"
#include "ComboSession.h"
#include "archipelago/ApPanel.h"
#include "archipelago/ApPaths.h"
#include "archipelago/ApSession.h"
#include "FleetSaveMenu.h"
#include "FleetGui.h"
#include "CrossWalk.h"
#include "SongWalk.h"
#include "DropWalk.h"
#include "EnemyWalk.h"
#include "FrameCapture.h"
#include "ItemRain.h"
#include "AssetCatalogue.h"
#include "PhotoWalk.h"
#include "ItemReview.h"
#include "ShareWalk.h"
#include "ItemWalk.h"
#include "TransitionWalk.h"
#include "SettingsSync.h"
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/config/Config.h>

int FleetSeedWorker(int argc, char* argv[]);

#ifdef _WIN32
#include <Windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <werapi.h>
#pragma comment(lib, "wer.lib")
#endif

#define BTN_CUSTOM_MODIFIER1 0x0040
#define BTN_CUSTOM_MODIFIER2 0x0080
#define BTN_CUSTOM_OCARINA_NOTE_D4 ((CONTROLLERBUTTONS_T)0x00010000)
#define BTN_CUSTOM_OCARINA_NOTE_F4 ((CONTROLLERBUTTONS_T)0x00020000)
#define BTN_CUSTOM_OCARINA_NOTE_A4 ((CONTROLLERBUTTONS_T)0x00040000)
#define BTN_CUSTOM_OCARINA_NOTE_B4 ((CONTROLLERBUTTONS_T)0x00080000)
#define BTN_CUSTOM_OCARINA_NOTE_D5 ((CONTROLLERBUTTONS_T)0x00100000)
#define BTN_CUSTOM_OCARINA_DISABLE_SONGS ((CONTROLLERBUTTONS_T)0x00200000)
#define BTN_CUSTOM_OCARINA_PITCH_UP ((CONTROLLERBUTTONS_T)0x00400000)
#define BTN_CUSTOM_OCARINA_PITCH_DOWN ((CONTROLLERBUTTONS_T)0x00800000)

namespace {

enum GameIndex { Oot, Mm, GameCount };
GameIndex IndexOf(const char* game);
void EndProcess(int status);

struct HostedGame {
    std::unique_ptr<GameModule> module;
    std::unique_ptr<GameFiber> fiber;
    std::string arrivalPayload;
    FleetDestination arrival = {};
};

struct Options {
    int mode = 0;
    bool internalRestore = false;
    int testChoice = -1;
    std::string ootDirectory;
    std::string mmDirectory;
    GameIndex start = Oot;
    int smokeFrames = 0;
    std::string seedPath;
    std::string sessionDirectory;
    int slot = 0;
    bool explicitStart = false;
    bool hidden = false;
    bool smokeItems = false;
    bool smokePortals = false;
    int smokeNew = -1;
    bool smokeResume = false;
    int smokeResets = 0;
    bool smokeCycle = false;
    bool smokeItemTest = false;
    std::optional<GameIndex> itemWalkGame;
    int itemWalkCopies = 4;
    std::string itemWalkOnly;
    std::string dropWalkRequests;
    std::string itemWalkSkip;
    bool itemWalkReverse = false;
    std::optional<GameIndex> dropWalkGame;
    std::optional<GameIndex> enemyWalkGame;
    EnemyWalk::Settings enemyWalk;
    std::optional<GameIndex> crossWalkGame;
    int crossWalkRounds = 2;
    bool songWalk = false;
    std::vector<std::string> itemRainGames;
    int itemRainWaves = 10;
    double itemRainSaveBudgetMs = 0.0;
    std::string shareWalkGame;
    bool assetCatalogue = false;
    std::string assetReview;
    std::string assetReviewOnly;
    std::string shareWalkBaseline;
    bool transitionWalk = false;
    std::string transitionWalkOnly;
    bool smokeName = false;
    bool smokeNativeGeneration = false;
    bool smokePages = false;
    std::string savesDirectory;
};

constexpr int kSmokeSwitches = 4;

std::array<HostedGame, GameCount> sGames;
GameIndex sActive = Oot;
std::optional<GameIndex> sPendingSwitch;
int sSmokeFrames = 0;
std::array<int, GameCount> sFramesRun = {};
int sSmokeSwitchesDone = 0;
int sSmokeResetsLeft = 0;
int sSmokeResetsDone = 0;
ComboSession sSession;
std::unique_ptr<ApSession> sArchipelago;
std::shared_ptr<FleetSaveMenu> sSaveMenu;
std::shared_ptr<FleetGui> sFleetGui;
bool sOpeningSession = false;
bool sCreatedStartPending = false;
SettingsSync sSettingsSync;
int sMode = 0;
GameIndex sLobbyStart = Oot;
std::array<bool, GameCount> sNativeFileSelect = {};
bool sMenusWarmed = false;
// Hiding the menu once the games warm closes any configuration, so the Archipelago setup opens only after that.
std::optional<std::string> sArchipelagoComboSlot;
bool sWasConfiguring = false;
bool sSmokeCycle = false;
bool sSmokeItemTest = false;
bool sSmokeGiftsSent = false;
std::optional<GameIndex> sItemWalkGame;
int sItemWalkCopies = 0;
std::string sItemWalkOnly;
std::string sDropWalkRequests;
std::string sItemWalkSkip;
bool sItemWalkReverse = false;
std::unique_ptr<ItemWalk> sItemWalk;
std::optional<GameIndex> sDropWalkGame;
std::unique_ptr<DropWalk> sDropWalk;
std::optional<GameIndex> sEnemyWalkGame;
EnemyWalk::Settings sEnemyWalkSettings;
std::unique_ptr<EnemyWalk> sEnemyWalk;
std::optional<GameIndex> sCrossWalkGame;
int sCrossWalkRounds = 0;
std::unique_ptr<CrossWalk> sCrossWalk;
bool sSongWalkRequested = false;
std::unique_ptr<SongWalk> sSongWalk;
std::vector<std::string> sItemRainGames;
int sItemRainWaves = 0;
double sItemRainSaveBudgetMs = 0.0;
std::unique_ptr<ItemRain> sItemRain;
std::string sShareWalkGame;
bool sAssetCatalogueRequested = false;
std::string sAssetReview;
std::string sAssetReviewOnly;
std::unique_ptr<PhotoWalk> sPhotoWalk;
std::unique_ptr<ItemReview> sItemReview;
std::string sShareWalkBaseline;
std::unique_ptr<ShareWalk> sShareWalk;
bool sTransitionWalkRequested = false;
std::string sTransitionWalkOnly;
std::unique_ptr<TransitionWalk> sTransitionWalk;
bool sSmokeCycleDone = false;
bool sSmokeName = false;
bool sSmokeNativeGeneration = false;
bool sNativeGenerationTestStarted = false;
bool sNativeGenerationReadyObserved = false;
bool sSmokePages = false;
bool sQuietCrashes = false;
bool sSmokePagesDone = false;
bool sSmokeSettingsDone = false;
bool sMenuExported = false;
std::string sOotDirectory;
std::string sMmDirectory;
bool sSmokeItems = false;
std::array<std::string, GameCount> sSmokeChecks;
bool sSmokePortals = false;
std::array<bool, GameCount> sSmokeArrivalPending = {};
std::array<int, GameCount> sSmokeArrivalWaits = {};
constexpr std::array<int, GameCount> kSmokeArrivalScenes = { 0, 27 };

bool IsComboMode() {
    return sMode == 0;
}

const char* GetSession() {
    return sSession.Enabled() ? sSession.Description().c_str() : nullptr;
}

const char* GetSaveDirectory(const char* game) {
    if (game == nullptr || (std::string(game) != "oot" && std::string(game) != "mm")) {
        return nullptr;
    }
    if (sSession.Enabled()) {
        return sSession.SaveDirectory(game).c_str();
    }
    return sSaveMenu != nullptr ? sSaveMenu->SaveDirectory(game).c_str() : nullptr;
}

std::filesystem::path InstallFolder() {
    if (const char* appImage = std::getenv("APPIMAGE")) {
        return std::filesystem::path(appImage).parent_path();
    }
    char* basePath = SDL_GetBasePath();
    std::filesystem::path folder = basePath != nullptr ? basePath : ".";
    SDL_free(basePath);
    for (std::filesystem::path bundle = folder; bundle != bundle.root_path(); bundle = bundle.parent_path()) {
        if (bundle.extension() == ".app") {
            return bundle.parent_path();
        }
    }
    return folder;
}

const char* GetModsDirectory(const char* game) {
    if (game == nullptr || (std::string(game) != "oot" && std::string(game) != "mm")) {
        return nullptr;
    }
    static std::map<std::string, std::string> paths;
    auto& path = paths[game];
    path = (InstallFolder() / "mods" / game).string();
    return path.c_str();
}

const char* GetSaveFilePath(const char* game, int slot, bool backup) {
    if (sSession.Enabled() || sSaveMenu == nullptr || game == nullptr ||
        (std::string(game) != "oot" && std::string(game) != "mm") || slot < 0 || slot > 2) {
        return nullptr;
    }
    static std::map<std::string, std::string> paths;
    auto& path = paths[game];
    path = sSaveMenu->SaveFile(game, slot, backup);
    return path.c_str();
}

bool HasSaveSlot(int slot) {
    return !sSession.Enabled() && sSaveMenu != nullptr && sSaveMenu->HasSlot(slot);
}

bool SelectFile(int slot) {
    return !sSession.Enabled() && sSaveMenu != nullptr && sSaveMenu->HasSlot(slot) && sSaveMenu->SelectNative(slot);
}

bool ConfigureRandomizer(int slot) {
    if (sSession.Enabled() || sSaveMenu == nullptr) {
        return false;
    }
    sSaveMenu->SelectNative(slot);
    sFleetGui->OpenRandomizer();
    return true;
}

bool HasGeneratedSeed(int slot) {
    return sSaveMenu != nullptr && sSaveMenu->SeedFor(slot).has_value();
}

bool GenerateRandomizer(int slot) {
    if (sSession.Enabled() || !sSaveMenu) {
        return false;
    }
    sFleetGui->GetMenu()->Hide();
    return sSaveMenu->GenerateNative(slot);
}

int RandomizerGenerationState() {
    return sSaveMenu ? sSaveMenu->GenerationState() : 0;
}

bool BeginCreatedSave(int slot) {
    if (!sSaveMenu) {
        return false;
    }
    const auto ready = sSaveMenu->SeedFor(slot);
    if (!ready) {
        return false;
    }
    sSession.Open(ready->seed, ready->session, ready->slot, sSmokeItems || sSmokeCycle);
    return true;
}

void FinishCreatedSave() {
    sSaveMenu->Complete();
    sCreatedStartPending = true;
    sOpeningSession = true;
    sFleetGui->GetMenu()->Hide();
    sFleetGui->SetLoading("Loading save...");
}

bool IsRuntimeTest() {
    return sSmokeFrames > 0;
}

bool ManageSave(const char* operation, int from, int to) {
    if (sSession.Enabled() || sSaveMenu == nullptr) {
        return false;
    }
    try {
        return sSaveMenu->Manage(operation != nullptr ? operation : "", from, to);
    } catch (const std::exception& error) {
        sSaveMenu->Fail(error.what());
        return true;
    }
}

std::vector<std::pair<std::string, std::string>> ListItemsOf(const HostedGame& hosted) {
    std::vector<std::pair<std::string, std::string>> items;
    const auto listed = nlohmann::json::parse(hosted.module->Request("ListItems"), nullptr, false);
    if (!listed.is_array()) {
        return items;
    }
    for (const auto& entry : listed) {
        const std::string name = entry.value("name", std::string{});
        items.emplace_back(name, entry.value("label", name));
    }
    return items;
}

void ListSessionItemsOnce(const char* game) {
    if (sSession.HasListedItems(game)) {
        return;
    }
    std::set<std::string> names;
    for (const auto& [name, label] : ListItemsOf(sGames[IndexOf(game)])) {
        names.insert(name);
    }
    sSession.SetListedItems(game, std::move(names));
}

const char* GetDeliveries(const char* game) {
    if (!sSession.Enabled() || game == nullptr || (std::string(game) != "oot" && std::string(game) != "mm")) {
        return nullptr;
    }
    ListSessionItemsOnce(game);
    return sSession.Deliveries(game).c_str();
}

bool ReportObtained(const char* game, const char* check) {
    try {
        const bool isReported = game != nullptr && check != nullptr && sSession.ReportObtained(game, check);
        if (isReported && sArchipelago) {
            sArchipelago->ReportObtained(game, check);
        }
        return isReported;
    } catch (const std::exception& error) {
        std::cerr << "[fleet] could not persist pickup: " << error.what() << "\n";
        return false;
    }
}

const char* GetSharedTiers(const char* game) {
    if (!sSession.Enabled() || game == nullptr || (std::string(game) != "oot" && std::string(game) != "mm")) {
        return nullptr;
    }
    return sSession.SharedTiers(game).c_str();
}

const char* GetBottles(const char* game) {
    static std::string bottles;
    bottles = game != nullptr ? sSession.BottlesFor(game) : std::string{};
    return bottles.c_str();
}

bool ReportBottles(const char* game, const char* bottles) {
    return game != nullptr && bottles != nullptr && sSession.ReportBottles(game, bottles);
}

const char* GetLiveState(const char* game) {
    static std::string state;
    state = game != nullptr ? sSession.LiveStateFor(game) : std::string{};
    return state.c_str();
}

bool ReportLiveState(const char* game, const char* state) {
    if (game == nullptr || state == nullptr) {
        return false;
    }
    return sSession.ReportLiveState(game, nlohmann::json::parse(state, nullptr, false));
}

bool ReportSharedTiers(const char* game, const char* tiers) {
    try {
        return game != nullptr && tiers != nullptr && sSession.ReportSharedTiers(game, nlohmann::json::parse(tiers));
    } catch (const std::exception& error) {
        std::cerr << "[fleet] could not persist shared tiers: " << error.what() << "\n";
        return false;
    }
}

int GoalReached(const char* game) {
    try {
        const bool isGoalMet = game == nullptr || sSession.ReportGoalReached(game);
        if (isGoalMet && sArchipelago) {
            sArchipelago->ReportGoal();
        }
        return isGoalMet ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "[fleet] could not persist the cleared game: " << error.what() << "\n";
        return 1;
    }
}

GameIndex IndexOf(const char* game) {
    return std::string(game) == "mm" ? Mm : Oot;
}

const FleetArchipelagoItem* ArchipelagoItems(const char* game, uint32_t* count) {
    if (count == nullptr) {
        return nullptr;
    }
    *count = 0;
    return sArchipelago && game != nullptr ? sArchipelago->Items(game, count) : nullptr;
}

void PumpArchipelago() {
    if (!sArchipelago) {
        return;
    }
    sArchipelago->Pump(&sSession);
    for (const std::string& message : sArchipelago->TakeMessages()) {
        sFleetGui->ShowArchipelagoMessage(message);
    }
}

void CommitSave() {
    sSession.RequestCommit();
}

void RequestFromStartedGames(const char* op) {
    for (const HostedGame& game : sGames) {
        if (game.module != nullptr && game.fiber != nullptr) {
            game.module->Request(op);
        }
    }
}

// The games and Fleet stay loaded: both games leave the session for their title, then their file select.
void CloseSession() {
    RequestFromStartedGames("EndSession");
    sSession.Close();
    RequestFromStartedGames("ReloadSaveFiles");
    sOpeningSession = false;
    sCreatedStartPending = false;
    sPendingSwitch.reset();
    if (sSaveMenu != nullptr) {
        sSaveMenu->Reopen();
    }
    sFleetGui->GetMenu()->Hide();
    sFleetGui->SetLoading("");
    std::cout << "[fleet] session closed; back to the file select" << std::endl;
}

void ResetActiveGame() {
    if (sSession.Enabled()) {
        CloseSession();
        return;
    }
    const auto& game = sGames[sActive].module;
    if (game != nullptr && game->Functions().Reset != nullptr) {
        game->Functions().Reset();
    }
}

bool DrawForeignItem(FleetItemDraw* request) {
    if (request == nullptr || request->owner == nullptr || request->item == nullptr || !IsComboMode()) {
        return false;
    }
    const auto& owner = sGames[IndexOf(request->owner)].module;
    return owner != nullptr && owner->Functions().DrawItem != nullptr && owner->Functions().DrawItem(request);
}

const char* GetItemTextboxIcon(const char* owner, const char* item) {
    static std::string path;
    path.clear();
    if (owner == nullptr || item == nullptr || !IsComboMode()) {
        return path.c_str();
    }
    const auto& module = sGames[IndexOf(owner)].module;
    if (module == nullptr) {
        return path.c_str();
    }
    const auto reply = nlohmann::json::parse(
        module->Request("ItemTextboxIcon", nlohmann::json{ { "item", item } }.dump()), nullptr, false);
    if (reply.is_object()) {
        path = reply.value("path", "");
    }
    return path.c_str();
}

GameIndex Other(GameIndex game) {
    return game == Oot ? Mm : Oot;
}

std::vector<std::string> sAttachProblems;

void LogFromGame(const char* game, FleetLogLevel level, const char* message) {
    std::cerr << "[" << game << "] " << message << "\n";
    if (level >= FLEET_LOG_ERROR) {
        sAttachProblems.push_back(message);
    }
}

void RequestSwitch(const char* fromGame, const FleetDestination* destination) {
    if (sMode != 0) {
        return;
    }
    const GameIndex target =
        destination != nullptr && destination->game != nullptr ? IndexOf(destination->game) : Other(IndexOf(fromGame));
    HostedGame& game = sGames[target];
    game.arrivalPayload = destination != nullptr && destination->payloadJson != nullptr ? destination->payloadJson : "";
    game.arrival = { .game = game.module->Name(),
                     .entrance = destination != nullptr ? destination->entrance : 0,
                     .payloadJson = game.arrivalPayload.empty() ? nullptr : game.arrivalPayload.c_str() };
    sPendingSwitch = target;
}

void EndProcess(int status);
std::filesystem::path InstallFolder();

void Activate(GameIndex target) {
    HostedGame& game = sGames[target];
    sActive = target;
    if (sMode == 0) {
        sFleetGui->Activate(game.module->Name());
    }
    if (sSession.Enabled()) {
        sSession.SetActiveGame(game.module->Name());
        if (game.fiber == nullptr ||
            !nlohmann::json::parse(game.module->Request("RuntimeStatus")).value("playing", false)) {
            sOpeningSession = true;
        }
    }
    if (game.fiber == nullptr) {
        game.fiber = std::make_unique<GameFiber>([&game] {
            try {
                game.module->Functions().Run(&game.arrival);
            } catch (const std::exception& error) {
                std::cerr << "[fleet] " << game.module->Name() << " stopped: " << error.what() << "\n";
#if defined(_WIN32) && !defined(FLEET_TEST_DRIVER)
                MessageBoxA(nullptr, error.what(), "Fleet", MB_OK | MB_ICONERROR);
#endif
                EndProcess(1);
            }
        });
    } else {
        Ship::GameRegistry::SetActive(game.module->Name());
        if (game.module->Functions().Arrive != nullptr) {
            game.module->Functions().Arrive(&game.arrival);
        }
    }
    game.fiber->SwitchTo();
}

bool CollectSmokeItem(GameIndex game) {
    if (!sSmokeItems || !sSmokeChecks[game].empty()) {
        return false;
    }
    const std::string source = sGames[game].module->Name();
    const auto seed = nlohmann::json::parse(sSession.Description()).at("seed");
    for (bool progressive : { true, false }) {
        for (const auto& foreign : seed.at("foreign")) {
            const std::string item = foreign.at("itemName");
            if (foreign.at("checkGame") != source || foreign.at("itemGame") == source || foreign.value("trap", false) ||
                (progressive && item.find("Progressive ") != 0)) {
                continue;
            }
            const std::string check = foreign.at("checkName");
            const auto result = nlohmann::json::parse(
                sGames[game].module->Request("RuntimeCollectCheck", nlohmann::json{ { "check", check } }.dump()),
                nullptr, false);
            if (!result.is_object() || !result.value("collected", false)) {
                throw std::runtime_error("runtime pickup smoke rejected " + source + ":" + check);
            }
            sSmokeChecks[game] = check;
            std::cout << "[fleet] pickup smoke " << source << ":" << check << " -> " << item << std::endl;
            return true;
        }
    }
    throw std::runtime_error("runtime pickup smoke needs foreign placements in both games");
}

void VerifySmokeItems() {
    if (!sSmokeItems) {
        return;
    }
    for (int source = 0; source < GameCount; source++) {
        const GameIndex target = Other(static_cast<GameIndex>(source));
        const auto status = nlohmann::json::parse(sGames[target].module->Request("RuntimeStatus"));
        const std::string id = std::string(sGames[source].module->Name()) + ":" + sSmokeChecks[source];
        const auto& received = status.at("received");
        if (std::find(received.begin(), received.end(), id) == received.end()) {
            throw std::runtime_error("runtime pickup smoke did not deliver " + id);
        }
    }
    std::cout << "[fleet] cross-game item smoke passed" << std::endl;
}

bool DrawIntoScratch(const std::string& owner, const std::string& item) {
    constexpr float kIdentity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    static std::vector<uint64_t> opa(1 << 16);
    static std::vector<uint64_t> xlu(1 << 16);
    FleetItemDraw request = { .owner = owner.c_str(),
                              .item = item.c_str(),
                              .opaHead = opa.data(),
                              .opaTail = opa.data() + opa.size(),
                              .xluHead = xlu.data(),
                              .xluTail = xlu.data() + xlu.size() };
    std::copy(std::begin(kIdentity), std::end(kIdentity), request.model);
    std::copy(std::begin(kIdentity), std::end(kIdentity), request.billboard);
    return DrawForeignItem(&request) && (request.opaHead != opa.data() || request.xluHead != xlu.data());
}

void VerifySmokeDraws() {
    if (!sSmokeItems) {
        return;
    }
    std::vector<std::string> failed;
    int drawn = 0;
    const auto session = nlohmann::json::parse(sSession.Description());
    for (const auto& foreign : session.at("seed").at("foreign")) {
        const std::string owner = foreign.at("itemGame");
        const std::string fake = foreign.value("fakeItemName", "");
        const std::string item = fake.empty() ? foreign.at("itemName").get<std::string>() : fake;
        if (!DrawIntoScratch(owner, item)) {
            failed.push_back(owner + ":" + item);
            continue;
        }
        ++drawn;
    }
    for (const auto& item : failed) {
        std::cout << "[fleet] owner could not draw " << item << std::endl;
    }
    if (!failed.empty()) {
        throw std::runtime_error("foreign item draw smoke failed for " + std::to_string(failed.size()) + " rewards");
    }
    std::cout << "[fleet] owners drew all " << drawn << " foreign rewards" << std::endl;
}

std::vector<std::array<std::string, 3>> ListAllItems() {
    std::vector<std::array<std::string, 3>> items;
    for (const auto& hosted : sGames) {
        for (const auto& [name, label] : ListItemsOf(hosted)) {
            items.push_back({ hosted.module->Name(), name, label });
        }
    }
    return items;
}

std::string RunItemDrawTest() {
    if (!sSession.Enabled()) {
        return "Open a Fleet save first: each game draws its items inside the session.";
    }
    const auto path = InstallFolder() / "logs" / "fleet-item-test.log";
    std::filesystem::create_directories(path.parent_path());
    std::ofstream log(path);
    std::vector<std::string> failed;
    const auto items = ListAllItems();
    for (const auto& [game, name, label] : items) {
        const std::string shown = game + ":" + name + " (" + label + ")";
        log << "icon  " << shown << std::endl;
        const auto& module = sGames[IndexOf(game.c_str())].module;
        FleetItemIcon icon = {};
        const bool hasIcon =
            module->Functions().ItemIcon != nullptr && module->Functions().ItemIcon(name.c_str(), &icon);
        log << "model " << shown << std::endl;
        const bool hasModel = DrawIntoScratch(game, name);
        if (!hasIcon || !hasModel) {
            failed.push_back(shown + (hasIcon ? "" : " [no icon]") + (hasModel ? "" : " [no model]"));
        }
    }
    log << "---\n" << items.size() << " items tested, " << failed.size() << " without an icon or a model" << std::endl;
    for (const auto& item : failed) {
        log << "  " << item << std::endl;
    }
    return std::to_string(items.size()) + " items tested, " + std::to_string(failed.size()) +
           " without an icon or a model. Log: " + path.string();
}

bool IsTrapItem(const std::string& name) {
    return name.find("Trap") != std::string::npos;
}

bool FindItemIcon(const std::string& game, const std::string& item, FleetItemIcon& icon) {
    const auto& module = sGames[IndexOf(game.c_str())].module;
    if (module == nullptr || module->Functions().ItemIcon == nullptr) {
        return false;
    }
    Ship::GameScope scope(module->Name());
    return module->Functions().ItemIcon(item.c_str(), &icon);
}

FleetGui::Items ListGivableItems() {
    FleetGui::Items items;
    if (!sSession.Enabled()) {
        return items;
    }
    for (const auto& [game, name, label] : ListAllItems()) {
        items.push_back({ game, name, label });
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const auto& left, const auto& right) { return left.label < right.label; });
    return items;
}

std::string GiveAllItems() {
    if (!sSession.Enabled()) {
        return "Open a Fleet save first.";
    }
    std::vector<std::pair<std::string, std::string>> gifts;
    for (const auto& [game, name, label] : ListAllItems()) {
        if (!IsTrapItem(name)) {
            gifts.emplace_back(game, name);
        }
    }
    sSession.GiveItems(gifts);
    return "Gave " + std::to_string(gifts.size()) + " items to both games.";
}

bool IsSelectedForWalk(const std::string& key) {
    if (key == sItemWalkSkip) {
        return false;
    }
    std::istringstream texts(sItemWalkOnly);
    std::string text;
    while (std::getline(texts, text, '|')) {
        if (key.find(text) != std::string::npos) {
            return true;
        }
    }
    return sItemWalkOnly.empty();
}

GiftWatch::Host CreateGiftHost(GameIndex watched) {
    const std::string game = sGames[watched].module->Name();
    const int slot = nlohmann::json::parse(sSession.Description()).at("slot");
    return {
        .readDeliveries = [game] { return nlohmann::json::parse(sSession.Deliveries(game)); },
        .readReceipts =
            [watched] {
                return nlohmann::json::parse(sGames[watched].module->Request("RuntimeStatus")).at("received");
            },
        .readSave = [game, slot] { return SaveDiff::ReadState(game, sSession.SaveDirectory(game), slot); },
    };
}

std::unique_ptr<ItemWalk> CreateItemWalk(GameIndex watched) {
    const std::string game = sGames[watched].module->Name();
    std::vector<ItemWalk::Item> items;
    for (const auto& [owner, name, label] : ListAllItems()) {
        if (IsSelectedForWalk(owner + ":" + name) && !IsTrapItem(name)) {
            items.push_back({ owner, name, label });
        }
    }
    if (sItemWalkReverse) {
        std::reverse(items.begin(), items.end());
    }
    ItemWalk::Host host = {
        .give = [](const ItemWalk::Item& item) { return sSession.GiveItem(item.game, item.name); },
        .gifts = CreateGiftHost(watched),
    };
    const auto log = InstallFolder() / "logs" / ("fleet-item-walk-" + game + ".jsonl");
    std::filesystem::create_directories(log.parent_path());
    std::cout << "[fleet] item walk: " << items.size() << " items x " << sItemWalkCopies << " into " << log.string()
              << std::endl;
    return std::make_unique<ItemWalk>(std::move(items), sItemWalkCopies, log, std::move(host));
}

bool IsWalkingItems() {
    return sItemWalk != nullptr && !sItemWalk->IsFinished();
}

std::vector<DropWalk::Setting> ListDropWalkSettings(GameIndex watched) {
    constexpr int kLowestResolution = 3;
    const bool isOot = watched == Oot;
    const char* skippedPickups =
        isOot ? "gRandoEnhancements.TimeSavers.SkipGetItemAnimation" : "gEnhancements.Cutscenes.SkipGetItemCutscenes";
    const char* collisionViewer = isOot ? "gDeveloperTools.ColViewer.Enabled" : "gCollisionViewer.Enabled";
    return { { skippedPickups, 0 }, { collisionViewer, 0 }, { CVAR_LOW_RES_MODE, kLowestResolution } };
}

std::string NameDroppedItem(const std::string& game, const std::string& owner, const std::string& name) {
    return owner == game ? name : "fleet:" + owner + ":" + name;
}

std::unique_ptr<DropWalk> CreateDropWalk(GameIndex watched) {
    const std::string game = sGames[watched].module->Name();
    std::vector<DropWalk::Item> items;
    std::istringstream requests(sDropWalkRequests);
    for (std::string request; std::getline(requests, request, '|');) {
        items.push_back({ request, request });
    }
    for (const auto& [owner, name, label] : ListAllItems()) {
        if (sDropWalkRequests.empty() && IsSelectedForWalk(owner + ":" + name) && !IsTrapItem(name)) {
            items.push_back({ NameDroppedItem(game, owner, name), label });
        }
    }
    DropWalk::Host host = {
        .settings = Ship::GameRegistry::Get(game)->ConsoleVariables,
        .forcedSettings = ListDropWalkSettings(watched),
        .capture = FrameCapture::Save,
        .requestSave = [game] { return sSession.GiveItem(game, "Recovery Heart"); },
        .gifts = CreateGiftHost(watched),
    };
    const auto log = InstallFolder() / "logs" / ("fleet-drop-walk-" + game + ".jsonl");
    std::filesystem::create_directories(log.parent_path());
    std::cout << "[fleet] drop walk: " << items.size() << " items into " << log.string() << std::endl;
    return std::make_unique<DropWalk>(std::move(items), log, std::move(host));
}

bool IsWalkingDrops() {
    return sDropWalk != nullptr && !sDropWalk->IsFinished();
}

std::unique_ptr<CrossWalk> CreateCrossWalk(GameIndex start) {
    std::vector<CrossWalk::Item> items;
    for (const auto& [owner, name, label] : ListAllItems()) {
        if (IsSelectedForWalk(owner + ":" + name) && !IsTrapItem(name)) {
            items.push_back({ owner, name });
        }
    }
    std::vector<std::string> route;
    for (int stop = 0; stop <= sCrossWalkRounds * GameCount; stop++) {
        route.push_back(sGames[stop % GameCount == 0 ? start : Other(start)].module->Name());
    }
    CrossWalk::Host host = {
        .give = [](const CrossWalk::Item& item) { return sSession.GiveItem(item.game, item.name); },
        .travel =
            [](const std::string& from, const std::string& to) {
                const FleetDestination destination = { .game = to.c_str() };
                RequestSwitch(from.c_str(), &destination);
            },
        .watchGifts = [](const std::string& game) { return CreateGiftHost(IndexOf(game.c_str())); },
    };
    const auto log = InstallFolder() / "logs" / "fleet-cross-walk.jsonl";
    std::filesystem::create_directories(log.parent_path());
    std::cout << "[fleet] cross walk: " << items.size() << " items over " << route.size() << " stops into "
              << log.string() << std::endl;
    return std::make_unique<CrossWalk>(std::move(items), std::move(route), log, std::move(host));
}

bool IsWalkingAcross() {
    return sCrossWalk != nullptr && !sCrossWalk->IsFinished();
}

std::unique_ptr<SongWalk> CreateSongWalk() {
    SongWalk::Host host = {
        .settings = [](const std::string& game) { return Ship::GameRegistry::Get(game)->ConsoleVariables; },
        .status =
            [](const std::string& game) {
                return nlohmann::json::parse(sGames[IndexOf(game.c_str())].module->Request("RuntimeStatus"), nullptr,
                                             false);
            },
        .travel =
            [](const std::string& from, const std::string& to) {
                const FleetDestination destination = { .game = sGames[IndexOf(to.c_str())].module->Name() };
                RequestSwitch(from.c_str(), &destination);
            },
    };
    const auto log = InstallFolder() / "logs" / "fleet-song-walk.jsonl";
    std::filesystem::create_directories(log.parent_path());
    std::cout << "[fleet] song walk into " << log.string() << std::endl;
    return std::make_unique<SongWalk>(log, std::move(host));
}

void AdvanceSongWalk(const char* game) {
    sSongWalk->Advance(game);
    if (sSongWalk->IsFinished()) {
        std::cout << "[fleet] song walk finished with " << sSongWalk->Failures() << " failures" << std::endl;
        EndProcess(sSongWalk->Failures() == 0 ? 0 : 1);
    }
}

nlohmann::json RequestTest(const std::string& game, const std::string& op, const nlohmann::json& input) {
    return nlohmann::json::parse(sGames[IndexOf(game.c_str())].module->Request(op, input.dump()), nullptr, false);
}

void TravelTo(const std::string& from, const std::string& to) {
    const FleetDestination destination = { .game = sGames[IndexOf(to.c_str())].module->Name() };
    RequestSwitch(from.c_str(), &destination);
}

// Assumes the caller holds the game's scope, so the resource manager is that game's.
std::shared_ptr<Ship::ResourceManager> ScopedResources() {
    return Ship::Context::GetRawInstance()->GetResourceManager();
}

nlohmann::json DescribeIconImage(const std::string& game, const std::string& path, const std::filesystem::path& folder) {
    if (path.empty()) {
        return { { "problem", "no icon resource" } };
    }
    Ship::GameScope scope(game);
    const auto texture = std::dynamic_pointer_cast<Fast::Texture>(ScopedResources()->LoadResource(path, true));
    if (texture == nullptr) {
        return { { "problem", "icon resource does not load" } };
    }
    const auto image = AssetCatalogue::DecodeTexture(*texture);
    if (!image) {
        return { { "problem", "icon format not decoded" }, { "format", static_cast<int>(texture->Type) } };
    }
    const std::string file = "icons/" + game + "/" + AssetCatalogue::FileNameOf(path) + ".png";
    AssetCatalogue::WritePng(folder / file, *image);
    const uint64_t pixels = AssetCatalogue::MixBytes(AssetCatalogue::kFnvOffset, image->rgba.data(), image->rgba.size());
    return { { "file", file },
             { "width", image->width },
             { "height", image->height },
             { "format", static_cast<int>(texture->Type) },
             { "hash", AssetCatalogue::HexOf(pixels) } };
}

nlohmann::json DescribeDisplayList(const std::string& game, const std::string& item) {
    constexpr float kIdentity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    static std::vector<uint64_t> opa(1 << 16);
    static std::vector<uint64_t> xlu(1 << 16);
    FleetItemDraw request = { .owner = game.c_str(),
                              .item = item.c_str(),
                              .opaHead = opa.data(),
                              .opaTail = opa.data() + opa.size(),
                              .xluHead = xlu.data(),
                              .xluTail = xlu.data() + xlu.size() };
    std::copy(std::begin(kIdentity), std::end(kIdentity), request.model);
    std::copy(std::begin(kIdentity), std::end(kIdentity), request.billboard);
    if (!DrawForeignItem(&request)) {
        return { { "drawn", false } };
    }
    Ship::GameScope scope(game);
    AssetCatalogue::DlWalker walker(ScopedResources());
    using Command = AssetCatalogue::Command;
    walker.Walk(reinterpret_cast<const Command*>(opa.data()), static_cast<const Command*>(request.opaHead));
    const auto summary = walker.Walk(reinterpret_cast<const Command*>(xlu.data()),
                                     static_cast<const Command*>(request.xluHead));
    return { { "drawn", summary.commands > 0 },
             { "fingerprint", AssetCatalogue::HexOf(summary.fingerprint) },
             { "commands", summary.commands },
             { "resources", summary.resources },
             { "issues", summary.issues } };
}

// Every item of both games: the icon the launcher and the textbox show, and the display list Fleet draws.
std::string WriteAssetCatalogue() {
    const auto folder = InstallFolder() / "logs" / "asset-catalogue";
    std::filesystem::remove_all(folder / "icons");
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& hosted : sGames) {
        const std::string game = hosted.module->Name();
        nlohmann::json names = nlohmann::json::array();
        const auto items = ListItemsOf(hosted);
        for (const auto& [name, label] : items) {
            names.push_back(name);
        }
        const nlohmann::json assets =
            RequestTest(game, "TestItemAssets", { { "items", names } }).value("assets", nlohmann::json::object());
        for (const auto& [name, label] : items) {
            std::cout << "[fleet] catalogue " << game << ":" << name << std::endl;
            const nlohmann::json icon = assets.value(name, nlohmann::json());
            const std::string iconPath = icon.is_object() ? icon.value("iconPath", std::string{}) : std::string{};
            rows.push_back({ { "game", game },
                             { "item", name },
                             { "label", label },
                             { "icon", icon },
                             { "iconImage", DescribeIconImage(game, iconPath, folder) },
                             { "displayList", DescribeDisplayList(game, name) } });
        }
    }
    std::ofstream(folder / "catalogue.json") << rows.dump(1);
    return std::to_string(rows.size()) + " items into " + (folder / "catalogue.json").string();
}

// Read beside the game, which keeps its input: A also closes the get-item textbox.
ItemReview::Choice ReadReviewChoice() {
    static uint16_t held = 0;
    const auto controller = Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0);
    if (controller == nullptr) {
        return ItemReview::Choice::None;
    }
    OSContPad pad = {};
    controller->ReadToPad(&pad);
    const uint16_t pressed = pad.button & ~held;
    held = pad.button;
    if (pressed & BTN_B) {
        return ItemReview::Choice::Reject;
    }
    if (pressed & BTN_Z) {
        return ItemReview::Choice::Back;
    }
    return (pressed & BTN_A) ? ItemReview::Choice::Keep : ItemReview::Choice::None;
}

std::string ToLowerCase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

// --asset-review-only: any of its "|"-separated words in the item's key or label; it narrows photos too.
bool MatchesItemFilter(const std::string& key, const std::string& label) {
    if (sAssetReviewOnly.empty()) {
        return true;
    }
    const std::string text = ToLowerCase(key + " " + label);
    std::stringstream words(ToLowerCase(sAssetReviewOnly));
    for (std::string word; std::getline(words, word, '|');) {
        if (!word.empty() && text.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string ReviewedVerdict(const nlohmann::json& review, const std::string& key) {
    return review.value(key, nlohmann::json::object()).value("verdict", std::string{});
}

// "pending" skips what was judged correct and "failed" shows only what was judged wrong, so a review picks up
// where it stopped; an item still on screen when the game went down is judged wrong for it.
bool IsSkippedInReview(const nlohmann::json& review, const std::string& key) {
    const std::string verdict = ReviewedVerdict(review, key);
    return (sAssetReview == "pending" && verdict == "correct") || (sAssetReview == "failed" && verdict != "wrong");
}

// Only souls cross over in the review: the other game's enemies and bosses are what each game must draw.
bool IsSoulItem(const std::string& name, const std::string& label) {
    return ToLowerCase(name + " " + label).find("soul") != std::string::npos;
}

// The game showing the items owns the first ones, then the others in hosting order.
std::vector<const HostedGame*> OwnersInReviewOrder(const HostedGame& shown) {
    std::vector<const HostedGame*> owners = { &shown };
    for (const auto& other : sGames) {
        if (&other != &shown) {
            owners.push_back(&other);
        }
    }
    return owners;
}

std::unique_ptr<ItemReview> CreateItemReview() {
    ItemReview::Review review = { InstallFolder() / "logs" / "asset-catalogue" / "ingame-review.json" };
    if (std::filesystem::exists(review.file)) {
        const nlohmann::json saved = nlohmann::json::parse(std::ifstream(review.file), nullptr, false);
        review.items = saved.value("items", review.items);
        const std::string crashed = saved.value("showing", std::string{});
        if (!crashed.empty()) {
            review.items[crashed] = { { "verdict", "wrong" }, { "note", "crashed the game" } };
            std::cout << "[fleet] item review: " << crashed << " crashed the game last time" << std::endl;
        }
    }
    // Each game shows its own items first, then every other game's as foreign get-items.
    std::vector<ItemReview::GameItems> items;
    for (const auto& shown : sGames) {
        const std::string game = shown.module->Name();
        std::vector<ItemReview::ReviewItem> entries;
        for (const HostedGame* owner : OwnersInReviewOrder(shown)) {
            for (const auto& [name, label] : ListItemsOf(*owner)) {
                const ItemReview::ReviewItem entry = { owner->module->Name(), name };
                const std::string key = ItemReview::KeyOf(game, entry);
                if (owner != &shown && !IsSoulItem(name, label)) {
                    continue;
                }
                if (!MatchesItemFilter(key, label) || IsSkippedInReview(review.items, key)) {
                    continue;
                }
                review.labels[key] = label;
                entries.push_back(entry);
            }
        }
        if (!entries.empty()) {
            items.emplace_back(game, std::move(entries));
        }
    }
    return std::make_unique<ItemReview>(
        std::move(items), std::move(review),
        ItemReview::Host{ .request = RequestTest,
                          .travel = TravelTo,
                          .readChoice = ReadReviewChoice,
                          .prompt = [](const std::string& game, const std::string& item,
                                       const std::string& text) { sFleetGui->SetReviewPrompt(game, item, text); } });
}

// Turbo needs vsync off and photos need the low-resolution framebuffer; both go back when the walk ends.
std::unique_ptr<PhotoWalk> CreatePhotoWalk() {
    constexpr int kLowestResolution = 3;
    const auto hostVariables = Ship::Context::GetRawInstance()->GetConsoleVariables();
    const int vsync = hostVariables->GetInteger(CVAR_VSYNC_ENABLED, 1);
    hostVariables->SetInteger(CVAR_VSYNC_ENABLED, 0);
    std::vector<std::pair<std::string, int>> lowResolution;
    std::vector<PhotoWalk::GameItems> items;
    for (const auto& hosted : sGames) {
        const std::string game = hosted.module->Name();
        const auto variables = Ship::GameRegistry::Get(game)->ConsoleVariables;
        lowResolution.emplace_back(game, variables->GetInteger(CVAR_LOW_RES_MODE, 0));
        variables->SetInteger(CVAR_LOW_RES_MODE, kLowestResolution);
        std::vector<std::string> names;
        for (const auto& [name, label] : ListItemsOf(hosted)) {
            if (MatchesItemFilter(game + ":" + name, label)) {
                names.push_back(name);
            }
        }
        items.emplace_back(game, std::move(names));
    }
    const auto restore = [hostVariables, vsync, lowResolution] {
        hostVariables->SetInteger(CVAR_VSYNC_ENABLED, vsync);
        for (const auto& [game, value] : lowResolution) {
            Ship::GameRegistry::Get(game)->ConsoleVariables->SetInteger(CVAR_LOW_RES_MODE, value);
        }
    };
    return std::make_unique<PhotoWalk>(
        std::move(items), InstallFolder() / "logs" / "asset-catalogue",
        PhotoWalk::Host{ .request = RequestTest, .travel = TravelTo, .finish = restore });
}

std::filesystem::path CreateTestLog(const std::string& name) {
    const auto log = InstallFolder() / "logs" / name;
    std::filesystem::create_directories(log.parent_path());
    return log;
}

std::unique_ptr<ItemRain> CreateItemRain() {
    return std::make_unique<ItemRain>(
        sItemRainGames, sItemRainWaves, sItemRainSaveBudgetMs, CreateTestLog("fleet-item-rain.jsonl"),
        ItemRain::Host{ .request = RequestTest,
                        .travel = TravelTo,
                        .slowestReportMilliseconds = [] { return sSession.SlowestReportMilliseconds(); } });
}

std::unique_ptr<ShareWalk> CreateShareWalk() {
    const nlohmann::json seed = nlohmann::json::parse(sSession.Description()).at("seed");
    const nlohmann::json baseline = sShareWalkBaseline.empty()
                                        ? nlohmann::json::object()
                                        : nlohmann::json::parse(std::ifstream(sShareWalkBaseline), nullptr, false);
    return std::make_unique<ShareWalk>(
        sShareWalkGame, seed, baseline.is_object() ? baseline : nlohmann::json::object(),
        CreateTestLog("fleet-share-walk-" + sShareWalkGame + ".jsonl"),
        ShareWalk::Host{ .request = RequestTest,
                         .travel = TravelTo,
                         .give = [](const auto& gifts) { sSession.GiveItems(gifts); },
                         .deliveries =
                             [](const std::string& game) {
                                 return nlohmann::json::parse(GetDeliveries(game.c_str()), nullptr, false);
                             },
                         .items = [](const std::string& game) {
                             std::set<std::string> names;
                             for (const auto& [name, label] : ListItemsOf(sGames[IndexOf(game.c_str())])) {
                                 names.insert(name);
                             }
                             return names;
                         } });
}

std::unique_ptr<TransitionWalk> CreateTransitionWalk() {
    constexpr int kLowestResolution = 3;
    // At native resolution the frame goes straight to the window and leaves no framebuffer to capture.
    for (const GameIndex game : { Oot, Mm }) {
        Ship::GameRegistry::Get(sGames[game].module->Name())
            ->ConsoleVariables->SetInteger(CVAR_LOW_RES_MODE, kLowestResolution);
    }
    const auto session = nlohmann::json::parse(sSession.Description());
    const nlohmann::json cross = session.at("seed").value("crossDungeonEntrances", nlohmann::json{});
    return std::make_unique<TransitionWalk>(cross, sTransitionWalkOnly, CreateTestLog("fleet-transition-walk.jsonl"),
                                            TransitionWalk::Host{ .request = RequestTest,
                                                                  .travel = TravelTo,
                                                                  .capture = FrameCapture::Save });
}

// Captures need the low-resolution framebuffer; the walk puts both forced settings back when it ends.
std::unique_ptr<EnemyWalk> CreateEnemyWalk(GameIndex watched) {
    constexpr int kLowestResolution = 3;
    const std::string game = sGames[watched].module->Name();
    const auto variables = Ship::GameRegistry::Get(game)->ConsoleVariables;
    const char* collisionViewer = watched == Oot ? "gDeveloperTools.ColViewer.Enabled" : "gCollisionViewer.Enabled";
    EnemyWalk::Host host = {
        .game = game,
        .gameDirectory = watched == Oot ? sOotDirectory : sMmDirectory,
        .settings = variables,
        .forcedSettings = { { collisionViewer, 0 },
                            { CVAR_LOW_RES_MODE, kLowestResolution },
                            { "gMods.EnemySpawnTester.Auto.OpenHint", sEnemyWalkSettings.opensHints ? 1 : 0 } },
        .saveSettings =
            [game, variables] {
                Ship::GameScope scope(game);
                variables->Save();
            },
        .request = RequestTest,
        .travel = TravelTo,
        .capture = FrameCapture::Save,
    };
    return std::make_unique<EnemyWalk>(sEnemyWalkSettings, EnemyWalk::LogPathOf(InstallFolder(), game),
                                       std::move(host));
}

template <class Walk> void AdvanceTestWalk(Walk& walk, const char* game, const char* name) {
    walk.Advance(game);
    if (walk.IsFinished()) {
        std::cout << "[fleet] " << name << " finished with " << walk.Failures() << " failures" << std::endl;
        EndProcess(walk.Failures() == 0 ? 0 : 1);
    }
}

void VerifySmokeGifts() {
    std::vector<std::string> missing;
    for (const auto& hosted : sGames) {
        const std::string game = hosted.module->Name();
        const auto deliveries = nlohmann::json::parse(GetDeliveries(game.c_str()));
        const auto status = nlohmann::json::parse(hosted.module->Request("RuntimeStatus"));
        const auto& received = status.at("received");
        for (const auto& delivery : deliveries) {
            const std::string id = delivery.at("id");
            const bool isGift = id.rfind("give:", 0) == 0;
            if (isGift && std::find(received.begin(), received.end(), id) == received.end()) {
                missing.push_back(id);
            }
        }
    }
    for (const auto& id : missing) {
        std::cout << "[fleet] gift not received: " << id << std::endl;
    }
    if (!missing.empty()) {
        throw std::runtime_error("give-all smoke left " + std::to_string(missing.size()) + " gifts undelivered");
    }
    std::cout << "[fleet] both games received every gift" << std::endl;
}

void RequestSmokeSwitch(GameIndex source) {
    const GameIndex target = Other(source);
    std::string payload;
    FleetDestination destination = { .game = sGames[target].module->Name() };
    if (sSmokePortals) {
        const auto session = nlohmann::json::parse(sSession.Description());
        const auto& cross = session.at("seed").at("crossDungeonEntrances");
        const auto& dungeons = cross.at("dungeons");
        const std::string id = target == Oot ? "oot:0" : "mm:woodfall";
        const auto dungeon =
            std::find_if(dungeons.begin(), dungeons.end(), [&](const auto& entry) { return entry.at("id") == id; });
        if (dungeon == dungeons.end()) {
            throw std::runtime_error("portal smoke needs Deku Tree and Woodfall in the seed");
        }
        const size_t index = static_cast<size_t>(dungeon - dungeons.begin());
        const auto& permutation = cross.at("permutation");
        bool crossArrival = false;
        for (size_t i = 0; i < permutation.size(); i++) {
            if (permutation.at(i) == index && dungeons.at(i).at("entrance").at("game") == source) {
                crossArrival = true;
            }
        }
        if (!crossArrival) {
            throw std::runtime_error("portal smoke fixture is not paired across games in this seed");
        }
        const int entrance = dungeon->at("entrance").at("index");
        payload = nlohmann::json{ { "cross", true }, { "entrance", entrance }, { "slot", session.at("slot") } }.dump();
        destination.entrance = entrance;
        destination.payloadJson = payload.c_str();
        sSmokeArrivalPending[target] = true;
        sSmokeArrivalWaits[target] = 0;
    }
    RequestSwitch(sGames[source].module->Name(), &destination);
}

// The reset a player picks from the menu, then the same file chosen again from Fleet's file select.
void ResetSessionForSmoke() {
    const int slot = nlohmann::json::parse(sSession.Description()).at("slot");
    ResetActiveGame();
    if (sSession.Enabled() || GetSession() != nullptr) {
        throw std::runtime_error("reset smoke: the session stayed open after the reset");
    }
    for (const HostedGame& hosted : sGames) {
        if (hosted.fiber == nullptr) {
            continue;
        }
        const auto status = nlohmann::json::parse(hosted.module->Request("RuntimeStatus"));
        if (status.value("playing", false)) {
            throw std::runtime_error(std::string("reset smoke: ") + hosted.module->Name() +
                                     " kept playing the closed session");
        }
    }
    if (sSaveMenu == nullptr || !sSaveMenu->Enabled()) {
        throw std::runtime_error("reset smoke: Fleet's file select did not come back");
    }
    sSaveMenu->Automate(slot, true);
    sSmokeResetsLeft--;
    sSmokeResetsDone++;
    sSmokeSwitchesDone = 0;
    std::cout << "[fleet] reset smoke: session closed in-process, reopening slot " << slot << std::endl;
}

void CountSmokeFrame(GameIndex game) {
    if (sSmokeFrames <= 0 || ++sFramesRun[game] % sSmokeFrames != 0) {
        return;
    }
    if (sMode != 0) {
        std::cout << "[fleet] solo native game smoke passed: " << game << std::endl;
        EndProcess(0);
    }
    if (!sSession.Enabled() && sSmokePages && !sSmokePagesDone && sFramesRun[Mm] > 0) {
        sFleetGui->SetLoading("");
        if (!sFleetGui->ExerciseRandomizerPages()) {
            return;
        }
        sSmokePagesDone = true;
        std::cout << "[fleet] unified randomizer pages rendered" << std::endl;
    }
    if (sSession.Enabled()) {
        const auto status = nlohmann::json::parse(sGames[game].module->Request("RuntimeStatus"), nullptr, false);
        if (!status.is_object() || !status.value("playing", false)) {
            return;
        }
        if (!sFleetGui->ExerciseMenu(sGames[game].module->Name())) {
            return;
        }
        if (sSmokePages && game == Oot && !sSmokePagesDone) {
            if (!sFleetGui->ExerciseRandomizerPages()) {
                return;
            }
            sSmokePagesDone = true;
            std::cout << "[fleet] randomizer pages rendered: OoT Shuffles, MM Check Pool, OoT Tricks/Glitches, "
                         "Item Pool, Plandomizer"
                      << std::endl;
        }
        if (sSmokeArrivalPending[game]) {
            if (status.at("scene") != kSmokeArrivalScenes[game]) {
                if (++sSmokeArrivalWaits[game] >= 10) {
                    throw std::runtime_error("portal smoke did not reach the requested native scene");
                }
                return;
            }
            sSmokeArrivalPending[game] = false;
            std::cout << "[fleet] portal arrival " << sGames[game].module->Name() << " reached scene "
                      << kSmokeArrivalScenes[game] << std::endl;
        }
        std::cout << "[fleet] runtime " << sGames[game].module->Name() << ": " << status.dump() << std::endl;
        if (sSmokeItemTest && !sSmokeGiftsSent) {
            sSmokeGiftsSent = true;
            std::cout << "[fleet] " << GiveAllItems() << std::endl;
        }
        const bool otherGameRan = sFramesRun[Other(game)] > 0;
        if (sItemWalkGame == game && sItemWalk == nullptr && otherGameRan) {
            sItemWalk = CreateItemWalk(game);
            return;
        }
        if (sDropWalkGame == game && sDropWalk == nullptr && otherGameRan) {
            sDropWalk = CreateDropWalk(game);
            return;
        }
        if (sEnemyWalkGame == game && sEnemyWalk == nullptr) {
            sEnemyWalk = CreateEnemyWalk(game);
            return;
        }
        if (sCrossWalkGame.has_value() && sCrossWalk == nullptr && otherGameRan) {
            sCrossWalk = CreateCrossWalk(*sCrossWalkGame);
            return;
        }
        if (sSongWalkRequested && sSongWalk == nullptr && otherGameRan) {
            sSongWalk = CreateSongWalk();
            return;
        }
        if (!sItemRainGames.empty() && sItemRain == nullptr && otherGameRan) {
            sItemRain = CreateItemRain();
            return;
        }
        if (!sShareWalkGame.empty() && sShareWalk == nullptr && otherGameRan) {
            sShareWalk = CreateShareWalk();
            return;
        }
        if (sAssetCatalogueRequested && sPhotoWalk == nullptr && sItemReview == nullptr && otherGameRan) {
            std::cout << "[fleet] asset catalogue: " << WriteAssetCatalogue() << std::endl;
            if (sAssetReview.empty()) {
                sPhotoWalk = CreatePhotoWalk();
            } else {
                sItemReview = CreateItemReview();
            }
            return;
        }
        if (sTransitionWalkRequested && sTransitionWalk == nullptr && otherGameRan) {
            sTransitionWalk = CreateTransitionWalk();
            return;
        }
        if (sSmokeCycle && game == Mm && !sSmokeCycleDone) {
            const auto cycle = nlohmann::json::parse(sGames[game].module->Request("RuntimeCycleSmoke"));
            if (!cycle.value("preserved", false) || !cycle.value("clockReset", false)) {
                throw std::runtime_error("MM cycle did not preserve flags while resetting the clock");
            }
            sSmokeCycleDone = true;
            std::cout << "[fleet] MM cycle persistence smoke passed" << std::endl;
        }
        if (CollectSmokeItem(game)) {
            return;
        }
        constexpr int kSwitchesBeforeReset = 2;
        if (sSmokeResetsLeft > 0 && sSmokeSwitchesDone >= kSwitchesBeforeReset) {
            ResetSessionForSmoke();
            return;
        }
    }
    std::cout << "[fleet] " << sGames[game].module->Name() << " ran " << sFramesRun[game] << " frames" << std::endl;
    if (sSmokeSwitchesDone++ == kSmokeSwitches) {
        VerifySmokeItems();
        VerifySmokeDraws();
        if (sSmokeItemTest || sItemWalk != nullptr || sCrossWalk != nullptr) {
            VerifySmokeGifts();
        }
        if (sSmokeItemTest) {
            std::cout << "[fleet] item test: " << RunItemDrawTest() << std::endl;
        }
        std::cout << "[fleet] native child menus smoke passed" << std::endl;
        if (sSmokePortals) {
            std::cout << "[fleet] cross-game arrival smoke passed" << std::endl;
        }
        if (sSmokeResetsDone > 0) {
            std::cout << "[fleet] in-process reset smoke passed: " << sSmokeResetsDone << " resets" << std::endl;
        }
        std::cout << "[fleet] smoke test passed" << std::endl;
        EndProcess(0);
    }
    RequestSmokeSwitch(game);
}

void CatchUpParkedGame(const char* activeGame) {
    constexpr int kCatchUpFrames = 300;
    static int sFramesSinceCatchUp;
    if (!sSession.Enabled()) {
        return;
    }
    if (++sFramesSinceCatchUp < kCatchUpFrames && !sSession.IsCommitRequested()) {
        return;
    }
    sFramesSinceCatchUp = 0;
    const HostedGame& parked = sGames[Other(IndexOf(activeGame))];
    if (parked.module != nullptr && parked.fiber != nullptr) {
        parked.module->Request("CatchUpParked");
    }
}

#ifdef _WIN32
// The host context has no crash handler and each game's handler shows a dialog, so quiet runs keep their own.
std::shared_ptr<Ship::CrashHandler> sQuietCrashHandler;

// A test run must never wait on a dialog: the crash is logged with its stack and the supervisor sees the exit.
LONG WINAPI ReportCrashQuietly(PEXCEPTION_POINTERS exception) {
    std::cerr << "[fleet] crash: exception 0x" << std::hex << exception->ExceptionRecord->ExceptionCode << " at "
              << exception->ExceptionRecord->ExceptionAddress << std::dec << std::endl;
    if (sQuietCrashHandler != nullptr && Ship::Context::GetRawInstance() != nullptr) {
        sQuietCrashHandler->PrintStack(exception->ContextRecord);
    }
    std::cerr.flush();
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// Each game installs libultraship's crash dialog when it starts, so the quiet filter is put back every frame.
void KeepCrashesQuiet() {
#ifdef _WIN32
    if (sQuietCrashes) {
        SetUnhandledExceptionFilter(ReportCrashQuietly);
    }
#endif
}

void SilenceCrashDialogs() {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    WerSetFlags(WER_FAULT_REPORTING_NO_UI);
    // Constructing the handler installs libultraship's dialog filter, replaced on the next line.
    sQuietCrashHandler = std::make_shared<Ship::CrashHandler>();
    SetUnhandledExceptionFilter(ReportCrashQuietly);
#endif
}

bool IsQuietRun(const Options& options) {
    return options.hidden || options.itemWalkGame || options.dropWalkGame || options.enemyWalkGame ||
           options.crossWalkGame || options.songWalk || !options.itemRainGames.empty() ||
           !options.shareWalkGame.empty() || options.assetCatalogue || options.transitionWalk;
}

void FrameEnd(const char* game) {
    KeepCrashesQuiet();
    CatchUpParkedGame(game);
    sSession.CommitIfRequested();
    PumpArchipelago();
    if (sCreatedStartPending) {
        sCreatedStartPending = false;
        const GameIndex target = sSession.ResumeGame() == "mm" ? Mm : Oot;
        const std::string payload = nlohmann::json{
            { "session", true }, { "slot", nlohmann::json::parse(sSession.Description()).at("slot") }
        }.dump();
        const FleetDestination destination = { .game = sGames[target].module->Name(), .payloadJson = payload.c_str() };
        sPendingSwitch.reset();
        if (target == IndexOf(game)) {
            sGames[target].module->Functions().Arrive(&destination);
        } else {
            RequestSwitch(game, &destination);
        }
        std::cout << "[fleet] named file starting in the existing window" << std::endl;
    }
    if (sMode == 0) {
        sFleetGui->Capture(game);
        sSettingsSync.Poll(game, [](const auto& owner, const auto& key) {
            sGames[IndexOf(owner.c_str())].module->Request("ApplySetting", nlohmann::json{ { "key", key } }.dump());
        });
    }
    if (sSaveMenu && sSaveMenu->Enabled() && !sMenusWarmed && sNativeFileSelect[IndexOf(game)]) {
        const auto other = Other(IndexOf(game));
        if (!sNativeFileSelect[other]) {
            const FleetDestination destination = { .game = sGames[other].module->Name() };
            RequestSwitch(game, &destination);
        } else {
            sMenusWarmed = true;
            sFleetGui->SetLoading("");
            sFleetGui->GetMenu()->Hide();
            const FleetDestination destination = { .game = sGames[sLobbyStart].module->Name() };
            RequestSwitch(game, &destination);
        }
    }
    if (sOpeningSession && sSession.Enabled()) {
        const auto status = nlohmann::json::parse(sGames[IndexOf(game)].module->Request("RuntimeStatus"));
        if (status.value("playing", false)) {
            sOpeningSession = false;
            sFleetGui->SetLoading("");
            if (sSmokeName) {
                std::cout << "[fleet] native name-entry save smoke passed" << std::endl;
                EndProcess(0);
            }
        }
    }
#ifdef FLEET_TEST_DRIVER
    if (sSmokePages && sMenusWarmed && !sMenuExported) {
        if (const char* output = std::getenv("FLEET_MENU_EXPORT")) {
            nlohmann::json inventory;
            bool ready = true;
            for (const auto& owner : { "oot", "mm" }) {
                auto& module = sGames[IndexOf(owner)].module;
                inventory[owner]["menu"] = nlohmann::json::parse(module->Request("MenuModel"));
                ready &= inventory[owner]["menu"].is_array();
                inventory[owner]["definitions"] = nlohmann::json::parse(module->Request("DumpSettingDefinitions"));
                inventory[owner]["settings"] = nlohmann::json::parse(module->Request("DumpSettings"));
            }
            if (ready) {
                std::ofstream exportFile(output);
                exportFile << inventory.dump(2);
                if (!exportFile) {
                    throw std::runtime_error("Could not export the native menu inventory");
                }
                sMenuExported = true;
                std::cout << "[fleet] native menu inventory exported" << std::endl;
            }
        }
    }
    if (sSmokePages && sMenusWarmed && !sSmokeSettingsDone) {
        auto oot = Ship::GameRegistry::Get("oot")->ConsoleVariables;
        auto mm = Ship::GameRegistry::Get("mm")->ConsoleVariables;
        auto poll = [&] {
            sSettingsSync.Poll(game, [](const auto& owner, const auto& key) {
                sGames[IndexOf(owner.c_str())].module->Request("ApplySetting", nlohmann::json{ { "key", key } }.dump());
            });
        };
        for (const auto& keys : { std::pair{ "gSettings.FreeLook.Enabled", "gEnhancements.Camera.FreeLook.Enable" },
                                  std::pair{ "gDeveloperTools.DebugEnabled", "gDeveloperTools.DebugEnabled" } }) {
            auto old = oot->Get(keys.first);
            const bool existed = old != nullptr;
            const int baseline = oot->GetInteger(keys.first, 0);
            oot->SetInteger(keys.first, !baseline);
            poll();
            if (mm->GetInteger(keys.second, -1) != !baseline) {
                throw std::runtime_error("OoT setting did not reach MM");
            }
            mm->SetInteger(keys.second, baseline);
            poll();
            if (oot->GetInteger(keys.first, -1) != baseline) {
                throw std::runtime_error("MM setting did not reach OoT");
            }
            if (!existed) {
                oot->ClearVariable(keys.first);
                poll();
            }
        }
        sSmokeSettingsDone = true;
        std::cout << "[fleet] shared settings callback smoke passed" << std::endl;
    }
#endif
    if (sSaveMenu != nullptr && sSaveMenu->Enabled() && sMenusWarmed) {
        if (sArchipelagoComboSlot) {
            sSaveMenu->OpenArchipelagoSetup(*sArchipelagoComboSlot);
            sArchipelagoComboSlot.reset();
        }
        if (sSmokeNativeGeneration && !sNativeGenerationTestStarted && IndexOf(game) == Oot) {
            sNativeGenerationTestStarted = true;
            sGames[Oot].module->Request("GenerateSeedTest");
            std::cout << "[fleet] native A generation requested" << std::endl;
        }
        sSaveMenu->Poll();
        if (sSmokeNativeGeneration && sNativeGenerationTestStarted && sFleetGui->GetMenu()->IsVisible()) {
            throw std::runtime_error("native seed generation opened the ImGui menu");
        }
        if (sSmokeNativeGeneration && sSaveMenu->GenerationState() == 3) {
            throw std::runtime_error("native seed generation failed; see the generation log");
        }
        if (sSaveMenu->IsConfiguring() && (!sWasConfiguring || !sFleetGui->GetMenu()->IsVisible())) {
            sFleetGui->OpenRandomizer();
        }
        sWasConfiguring = sSaveMenu->IsConfiguring();
        if (sSmokeName && sSaveMenu->Generated(0)) {
            if (!sSmokeNativeGeneration || sNativeGenerationReadyObserved) {
                sGames[Oot].module->Request("CreateNamedSaveTest");
            }
            sNativeGenerationReadyObserved = true;
        }
        if (const auto ready = sSaveMenu->Pending()) {
            try {
                sSession.Open(ready->seed, ready->session, ready->slot, sSmokeItems || sSmokeCycle);
                sOpeningSession = true;
                sFleetGui->SetLoading("Loading save...");
                sSaveMenu->Complete();
                sFleetGui->GetMenu()->Hide();
                const GameIndex target = sSession.ResumeGame() == "mm" ? Mm : Oot;
                const std::string payload = nlohmann::json{ { "session", true }, { "slot", ready->slot } }.dump();
                const FleetDestination destination = { .game = sGames[target].module->Name(),
                                                       .payloadJson = payload.c_str() };
                sPendingSwitch.reset();
                if (target == IndexOf(game)) {
                    sGames[target].module->Functions().Arrive(&destination);
                } else {
                    RequestSwitch(game, &destination);
                }
                std::cout << "[fleet] in-game file opened: slot " << ready->slot << std::endl;
            } catch (const std::exception& error) { sSaveMenu->Fail(error.what()); }
        }
    }
    if (IsWalkingItems()) {
        sItemWalk->Advance();
    } else if (IsWalkingDrops()) {
        sDropWalk->Advance();
    } else if (IsWalkingAcross()) {
        sCrossWalk->Advance(game);
    } else if (sSongWalk != nullptr) {
        AdvanceSongWalk(game);
    } else if (sItemRain != nullptr) {
        AdvanceTestWalk(*sItemRain, game, "item rain");
    } else if (sShareWalk != nullptr) {
        AdvanceTestWalk(*sShareWalk, game, "share walk");
    } else if (sPhotoWalk != nullptr) {
        AdvanceTestWalk(*sPhotoWalk, game, "photo walk");
    } else if (sItemReview != nullptr) {
        AdvanceTestWalk(*sItemReview, game, "item review");
    } else if (sTransitionWalk != nullptr) {
        AdvanceTestWalk(*sTransitionWalk, game, "transition walk");
    } else if (sEnemyWalk != nullptr) {
        AdvanceTestWalk(*sEnemyWalk, game, "enemy walk");
    } else if (sSaveMenu == nullptr || !sSaveMenu->Enabled()) {
        CountSmokeFrame(IndexOf(game));
    }
    if (!sPendingSwitch || *sPendingSwitch == IndexOf(game)) {
        sPendingSwitch.reset();
        return;
    }
    const GameIndex target = *sPendingSwitch;
    sPendingSwitch.reset();
    Activate(target);
    Ship::GameRegistry::SetActive(game);
}

bool HasFileSelect() {
    return sSaveMenu != nullptr && sSaveMenu->Enabled();
}

bool FileSelect() {
    if (sOpeningSession) {
        return true;
    }
    if (sSession.Enabled()) {
        CloseSession();
        return true;
    }
    if (!HasFileSelect()) {
        return false;
    }
    sNativeFileSelect[sActive] = true;
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui->GetGuiWindow("FleetFiles") == nullptr) {
        gui->AddGuiWindow(sSaveMenu);
    }
    const bool configuring = sSaveMenu->OpenFiles();
    return !sMenusWarmed || configuring;
}

void* HostFunction(const char* name) {
    const std::string service = name;
    if (service == "IsComboMode") {
        return reinterpret_cast<void*>(IsComboMode);
    }
    if (service == "HasFileSelect") {
        return reinterpret_cast<void*>(HasFileSelect);
    }
    if (service == "FileSelect") {
        return reinterpret_cast<void*>(FileSelect);
    }
    if (service == "SelectFile") {
        return reinterpret_cast<void*>(SelectFile);
    }
    if (service == "ConfigureRandomizer") {
        return reinterpret_cast<void*>(ConfigureRandomizer);
    }
    if (service == "HasGeneratedSeed") {
        return reinterpret_cast<void*>(HasGeneratedSeed);
    }
    if (service == "GenerateRandomizer") {
        return reinterpret_cast<void*>(GenerateRandomizer);
    }
    if (service == "RandomizerGenerationState") {
        return reinterpret_cast<void*>(RandomizerGenerationState);
    }
    if (service == "BeginCreatedSave") {
        return reinterpret_cast<void*>(BeginCreatedSave);
    }
    if (service == "FinishCreatedSave") {
        return reinterpret_cast<void*>(FinishCreatedSave);
    }
    if (service == "IsRuntimeTest") {
        return reinterpret_cast<void*>(IsRuntimeTest);
    }
    if (service == "GetSaveFilePath") {
        return reinterpret_cast<void*>(GetSaveFilePath);
    }
    if (service == "HasSaveSlot") {
        return reinterpret_cast<void*>(HasSaveSlot);
    }
    if (service == "ManageSave") {
        return reinterpret_cast<void*>(ManageSave);
    }
    if (service == "GetSharedTiers") {
        return reinterpret_cast<void*>(GetSharedTiers);
    }
    if (service == "ReportSharedTiers") {
        return reinterpret_cast<void*>(ReportSharedTiers);
    }
    if (service == "GetBottles") {
        return reinterpret_cast<void*>(GetBottles);
    }
    if (service == "ReportBottles") {
        return reinterpret_cast<void*>(ReportBottles);
    }
    if (service == "GetLiveState") {
        return reinterpret_cast<void*>(GetLiveState);
    }
    if (service == "ReportLiveState") {
        return reinterpret_cast<void*>(ReportLiveState);
    }
    if (service == "GetSession") {
        return reinterpret_cast<void*>(GetSession);
    }
    if (service == "GetSaveDirectory") {
        return reinterpret_cast<void*>(GetSaveDirectory);
    }
    if (service == "GetModsDirectory") {
        return reinterpret_cast<void*>(GetModsDirectory);
    }
    if (service == "GetDeliveries") {
        return reinterpret_cast<void*>(GetDeliveries);
    }
    if (service == "ReportObtained") {
        return reinterpret_cast<void*>(ReportObtained);
    }
    if (service == "DrawForeignItem") {
        return reinterpret_cast<void*>(DrawForeignItem);
    }
    if (service == "GetItemTextboxIcon") {
        return reinterpret_cast<void*>(GetItemTextboxIcon);
    }
    if (service == "ResetActiveGame") {
        return reinterpret_cast<void*>(ResetActiveGame);
    }
    if (service == "CommitSave") {
        return reinterpret_cast<void*>(CommitSave);
    }
    if (service == "Log") {
        return reinterpret_cast<void*>(LogFromGame);
    }
    if (service == "RequestSwitch") {
        return reinterpret_cast<void*>(RequestSwitch);
    }
    if (service == "FrameEnd") {
        return reinterpret_cast<void*>(FrameEnd);
    }
    if (service == "GoalReached") {
        return reinterpret_cast<void*>(GoalReached);
    }
    if (service == "ArchipelagoItems") {
        return reinterpret_cast<void*>(ArchipelagoItems);
    }
    return nullptr;
}

void LoadInstallFile(Options& options) {
    const std::filesystem::path folder = InstallFolder();
    options.ootDirectory = (folder / "Ship").string();
    options.mmDirectory = (folder / "2Ship").string();
    std::ifstream file(folder / "fleet.json");
    const nlohmann::json config = nlohmann::json::parse(file, nullptr, false);
    if (!config.is_object()) {
        return;
    }
    for (auto [key, directory] :
         { std::pair{ "oot", &options.ootDirectory }, std::pair{ "mm", &options.mmDirectory } }) {
        const std::string path = config.value(key, "");
        if (!path.empty()) {
            *directory = (folder / path).lexically_normal().string();
        }
    }
}

bool ParseOptions(int argc, char* argv[], Options& options) {
    LoadInstallFile(options);
    if (argc % 2 == 0) {
        return false;
    }
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string flag = argv[i];
        const std::string value = argv[i + 1];
        if (flag == "--oot") {
            options.ootDirectory = value;
        } else if (flag == "--mm") {
            options.mmDirectory = value;
#ifdef FLEET_TEST_DRIVER
        } else if (flag == "--test-native-generation" && value == "1") {
            options.smokeNativeGeneration = true;
        } else if (flag == "--test-menu-pages" && value == "1") {
            options.smokePages = true;
        } else if (flag == "--test-name" && value == "1") {
            options.smokeName = true;
        } else if (flag == "--test-choice" && (value == "0" || value == "1" || value == "2")) {
            options.testChoice = std::stoi(value);
        } else if (flag == "--test-game" && (value == "0" || value == "1" || value == "2")) {
            options.mode = std::stoi(value);
            options.internalRestore = true;
        } else if (flag == "--start" && (value == "oot" || value == "mm")) {
            options.start = value == "mm" ? Mm : Oot;
            options.explicitStart = true;
        } else if (flag == "--seed") {
            options.seedPath = value;
        } else if (flag == "--session") {
            options.sessionDirectory = value;
        } else if (flag == "--slot" && (value == "0" || value == "1" || value == "2")) {
            options.slot = std::stoi(value);
        } else if (flag == "--smoke") {
            options.smokeFrames = std::atoi(value.c_str());
        } else if (flag == "--hidden" && (value == "0" || value == "1")) {
            options.hidden = value == "1";
        } else if (flag == "--smoke-items" && (value == "0" || value == "1")) {
            options.smokeItems = value == "1";
        } else if (flag == "--smoke-portals" && (value == "0" || value == "1")) {
            options.smokePortals = value == "1";
        } else if (flag == "--saves") {
            options.savesDirectory = value;
        } else if (flag == "--smoke-new" && (value == "0" || value == "1" || value == "2")) {
            options.smokeNew = std::stoi(value);
        } else if (flag == "--smoke-resume" && (value == "0" || value == "1")) {
            options.smokeResume = value == "1";
        } else if (flag == "--smoke-reset" && std::atoi(value.c_str()) > 0) {
            options.smokeResets = std::atoi(value.c_str());
        } else if (flag == "--smoke-cycle" && (value == "0" || value == "1")) {
            options.smokeCycle = value == "1";
        } else if (flag == "--item-test" && (value == "0" || value == "1")) {
            options.smokeItemTest = value == "1";
        } else if (flag == "--item-walk" && (value == "oot" || value == "mm")) {
            options.itemWalkGame = value == "mm" ? Mm : Oot;
        } else if (flag == "--item-walk-copies" && std::atoi(value.c_str()) > 0) {
            options.itemWalkCopies = std::atoi(value.c_str());
        } else if (flag == "--item-walk-only") {
            options.itemWalkOnly = value;
        } else if (flag == "--drop-walk-requests") {
            options.dropWalkRequests = value;
        } else if (flag == "--item-walk-skip") {
            options.itemWalkSkip = value;
        } else if (flag == "--item-walk-reverse" && (value == "0" || value == "1")) {
            options.itemWalkReverse = value == "1";
        } else if (flag == "--drop-walk" && (value == "oot" || value == "mm")) {
            options.dropWalkGame = value == "mm" ? Mm : Oot;
        } else if (flag == "--cross-walk" && (value == "oot" || value == "mm")) {
            options.crossWalkGame = value == "mm" ? Mm : Oot;
        } else if (flag == "--cross-walk-rounds" && std::atoi(value.c_str()) > 0) {
            options.crossWalkRounds = std::atoi(value.c_str());
        } else if (flag == "--song-walk" && (value == "0" || value == "1")) {
            options.songWalk = value == "1";
        } else if (flag == "--item-rain" && (value == "oot" || value == "mm" || value == "both")) {
            options.itemRainGames = value == "both" ? std::vector<std::string>{ "mm", "oot" }
                                                    : std::vector<std::string>{ value };
        } else if (flag == "--item-rain-waves" && std::atoi(value.c_str()) > 0) {
            options.itemRainWaves = std::atoi(value.c_str());
        } else if (flag == "--share-walk" && (value == "oot" || value == "mm")) {
            options.shareWalkGame = value;
        } else if (flag == "--asset-catalogue" && value == "1") {
            options.assetCatalogue = true;
        } else if (flag == "--asset-review" && (value == "pending" || value == "failed" || value == "all")) {
            options.assetReview = value;
        } else if (flag == "--asset-review-only") {
            options.assetReviewOnly = value;
        } else if (flag == "--share-walk-baseline") {
            options.shareWalkBaseline = value;
        } else if (flag == "--save-budget-ms" && std::atof(value.c_str()) > 0.0) {
            options.itemRainSaveBudgetMs = std::atof(value.c_str());
        } else if (flag == "--transition-walk" && (value == "0" || value == "1")) {
            options.transitionWalk = value == "1";
        } else if (flag == "--transition-walk-only") {
            options.transitionWalkOnly = value;
        } else if (flag == "--enemy-walk" && (value == "oot" || value == "mm")) {
            options.enemyWalkGame = value == "mm" ? Mm : Oot;
        } else if (flag == "--enemy-walk-only") {
            options.enemyWalk.only = value;
        } else if (flag == "--enemy-walk-hints" && (value == "0" || value == "1")) {
            options.enemyWalk.opensHints = value == "1";
        } else if (flag == "--enemy-walk-frames" && std::atoi(value.c_str()) > 0) {
            options.enemyWalk.frames = std::atoi(value.c_str());
        } else if (flag == "--enemy-walk-scenes" && std::atoi(value.c_str()) > 0) {
            options.enemyWalk.scenesPerEnemy = std::atoi(value.c_str());
        } else if (flag == "--enemy-walk-spawns") {
            options.enemyWalk.spawns = value;
        } else if (flag == "--enemy-walk-candidates") {
            options.enemyWalk.candidates = value;
        } else if (flag == "--enemy-walk-shard" && EnemyWalk::ParseShard(value, options.enemyWalk)) {
        } else if (flag == "--enemy-walk-resume" && std::atoi(value.c_str()) >= 0) {
            options.enemyWalk.resume = static_cast<size_t>(std::atoi(value.c_str()));
#endif
        } else {
            return false;
        }
    }
    return (!options.ootDirectory.empty() || !options.mmDirectory.empty()) &&
           (!(options.smokeItems || options.smokePortals || options.smokeCycle) ||
            (options.smokeFrames > 0 && (!options.seedPath.empty() || options.smokeNew >= 0))) &&
           (options.smokeNew < 0 || (options.hidden && options.smokeFrames > 0 && options.seedPath.empty())) &&
           (options.smokeResets == 0 || options.smokeNew >= 0);
}

void CreateHostContext() {
    Ship::Context* context = Ship::Context::CreateUninitializedInstance("Fleet", "fleet", "fleetship.json");
    context->InitLogging();
    context->InitConfiguration();
    context->InitConsoleVariables();
}

bool AttachGames(const Options& options) {
    const std::array<std::pair<std::string, const char*>, GameCount> installs = {
        { { options.ootDirectory, "soh" }, { options.mmDirectory, "2ship" } }
    };
    for (int game = 0; game < GameCount; game++) {
        if ((sMode == 1 && game != Oot) || (sMode == 2 && game != Mm)) {
            continue;
        }
        sGames[game].module = GameModule::Load(installs[game].first, installs[game].second, HostFunction);
        if (sGames[game].module == nullptr && sAttachProblems.empty()) {
            sAttachProblems.push_back(std::string(installs[game].second) + " could not be loaded from " +
                                      installs[game].first + ".");
        }
    }
    if (sAttachProblems.empty()) {
        return true;
    }
    std::string text;
    for (const std::string& problem : sAttachProblems) {
        text += problem + "\n\n";
    }
    std::cerr << "[fleet] cannot start: " << text;
    if (!sQuietCrashes) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fleet cannot start", text.c_str(), nullptr);
    }
    return false;
}

#ifdef _WIN32
// A hidden test is silent. The mute belongs to this process's audio session, so no game setting changes.
void MuteThisProcess() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* devices = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices)))) {
        return;
    }
    IMMDevice* speakers = nullptr;
    IAudioSessionManager* sessions = nullptr;
    ISimpleAudioVolume* volume = nullptr;
    if (SUCCEEDED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &speakers)) &&
        SUCCEEDED(speakers->Activate(__uuidof(IAudioSessionManager), CLSCTX_ALL, nullptr,
                                     reinterpret_cast<void**>(&sessions))) &&
        SUCCEEDED(sessions->GetSimpleAudioVolume(nullptr, FALSE, &volume))) {
        volume->SetMute(TRUE, nullptr);
    }
    for (IUnknown* object : std::initializer_list<IUnknown*>{ volume, sessions, speakers, devices }) {
        if (object != nullptr) {
            object->Release();
        }
    }
}
#endif

void OpenWindow(const Options& options) {
    Ship::Context* context = Ship::Context::GetRawInstance();
    context->InitControlDeck(std::make_shared<LUS::ControlDeck>(std::vector<CONTROLLERBUTTONS_T>({
        BTN_CUSTOM_MODIFIER1,
        BTN_CUSTOM_MODIFIER2,
        BTN_CUSTOM_OCARINA_NOTE_D4,
        BTN_CUSTOM_OCARINA_NOTE_F4,
        BTN_CUSTOM_OCARINA_NOTE_A4,
        BTN_CUSTOM_OCARINA_NOTE_B4,
        BTN_CUSTOM_OCARINA_NOTE_D5,
        BTN_CUSTOM_OCARINA_DISABLE_SONGS,
        BTN_CUSTOM_OCARINA_PITCH_UP,
        BTN_CUSTOM_OCARINA_PITCH_DOWN,
    })));
    auto archive = std::filesystem::path(options.ootDirectory) / "soh.o2r";
    if (!std::filesystem::exists(archive)) {
        archive = std::filesystem::path(options.mmDirectory) / "2ship.o2r";
    }
    const std::string shaders = archive.string();
    context->InitResourceManager({ shaders }, {}, 3, true);
    context->InitConsole();
    sFleetGui = std::make_shared<FleetGui>();
    context->InitWindow(std::make_shared<Fast::Fast3dWindow>(sFleetGui));
    context->InitFileDropMgr();
    context->GetFileDropMgr()->RegisterDropHandler([](char* path) {
        const auto notice = sSaveMenu != nullptr ? sSaveMenu->AddDroppedSeed(path) : std::nullopt;
        if (!notice) {
            return false;
        }
        if (const auto& module = sGames[sActive].module) {
            module->Request("Notify", nlohmann::json{ { "message", *notice } }.dump());
        }
        return true;
    });
#ifdef _WIN32
    EnumWindows(
        [](HWND window, LPARAM) -> BOOL {
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            if (process == GetCurrentProcessId()) {
                const auto icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
                SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
                SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
            }
            return TRUE;
        },
        0);
#endif
    if (options.hidden) {
#ifdef _WIN32
        MuteThisProcess();
        EnumWindows(
            [](HWND window, LPARAM) -> BOOL {
                DWORD process = 0;
                GetWindowThreadProcessId(window, &process);
                if (process == GetCurrentProcessId()) {
                    ShowWindow(window, SW_HIDE);
                }
                return TRUE;
            },
            0);
#else
        if (SDL_Window* window = SDL_GL_GetCurrentWindow()) {
            SDL_HideWindow(window);
        }
#endif
    }
}

void RegisterCommands() {
    Ship::Context::GetRawInstance()->GetConsole()->AddCommand(
        "fleet_switch", { [](std::shared_ptr<Ship::Console>, const std::vector<std::string>&, std::string*) {
                             const FleetDestination destination = { .game = sGames[Other(sActive)].module->Name() };
                             RequestSwitch(sGames[sActive].module->Name(), &destination);
                             return 0;
                         },
                          "Parks the running game and continues in the other one" });
    Ship::Context::GetRawInstance()->GetConsole()->AddCommand(
        "fleet_item_test", { [](std::shared_ptr<Ship::Console>, const std::vector<std::string>&, std::string* output) {
                                if (output != nullptr) {
                                    *output = RunItemDrawTest();
                                }
                                return 0;
                            },
                             "Draws the icon and model of every item of both games, logging each one first" });
    Ship::Context::GetRawInstance()->GetConsole()->AddCommand(
        "fleet_give_all", { [](std::shared_ptr<Ship::Console>, const std::vector<std::string>&, std::string* output) {
                               if (output != nullptr) {
                                   *output = GiveAllItems();
                               }
                               return 0;
                           },
                            "Gives every item of both games, traps excepted" });
}

int RunFleet(int argc, char* argv[]) {
#ifndef _WIN32
    // OpenSSL writes to the Archipelago socket with write(), which raises SIGPIPE once the server hangs up.
    std::signal(SIGPIPE, SIG_IGN);
#endif
    Options options;
    if (!ParseOptions(argc, argv, options)) {
        const char* usage = "Fleet needs your Ship of Harkinian and 2 Ship 2 Harkinian installations\n"
                            "in the Ship and 2Ship folders next to Fleet.\n\n"
                            "To keep them elsewhere, write their paths in fleet.json next to Fleet:\n"
                            "{ \"oot\": \"D:/Games/soh\", \"mm\": \"D:/Games/2ship\" }\n\n"
                            "Open Fleet and choose Combo, Ocarina of Time or Majora's Mask.";
        std::cerr << usage << "\n";
#ifndef FLEET_TEST_DRIVER
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fleet", usage, nullptr);
#endif
        return 2;
    }
    sQuietCrashes = IsQuietRun(options);
    if (sQuietCrashes) {
        SilenceCrashDialogs();
    }
    RemoveReplacedFiles(InstallFolder());
    const bool isPlayerRun = options.smokeFrames == 0;
    if (isPlayerRun && !EnsureGameInstalls(InstallFolder(), options.ootDirectory, options.mmDirectory)) {
        return 2;
    }
    if (isPlayerRun && !EnsureGameAssets(InstallFolder(), std::filesystem::u8path(options.ootDirectory),
                                         std::filesystem::u8path(options.mmDirectory))) {
        return 2;
    }
    sSmokeFrames = options.smokeFrames;
    sSmokeItems = options.smokeItems;
    sSmokePortals = options.smokePortals;
    sSmokeCycle = options.smokeCycle;
    sSmokeResetsLeft = options.smokeResets;
    sSmokeItemTest = options.smokeItemTest;
    sItemWalkGame = options.itemWalkGame;
    sItemWalkCopies = options.itemWalkCopies;
    sItemWalkOnly = options.itemWalkOnly;
    sDropWalkRequests = options.dropWalkRequests;
    sItemWalkSkip = options.itemWalkSkip;
    sItemWalkReverse = options.itemWalkReverse;
    sDropWalkGame = options.dropWalkGame;
    sCrossWalkGame = options.crossWalkGame;
    sCrossWalkRounds = options.crossWalkRounds;
    sSongWalkRequested = options.songWalk;
    sItemRainGames = options.itemRainGames;
    sItemRainWaves = options.itemRainWaves;
    sItemRainSaveBudgetMs = options.itemRainSaveBudgetMs;
    sShareWalkGame = options.shareWalkGame;
    sAssetCatalogueRequested = options.assetCatalogue || !options.assetReview.empty();
    sAssetReview = options.assetReview;
    sAssetReviewOnly = options.assetReviewOnly;
    sShareWalkBaseline = options.shareWalkBaseline;
    sTransitionWalkRequested = options.transitionWalk;
    sTransitionWalkOnly = options.transitionWalkOnly;
    sEnemyWalkGame = options.enemyWalkGame;
    sEnemyWalkSettings = options.enemyWalk;
    sSmokeName = options.smokeName;
    sSmokeNativeGeneration = options.smokeNativeGeneration;
    sSmokePages = options.smokePages;
    sOotDirectory = std::filesystem::absolute(options.ootDirectory).string();
    sMmDirectory = std::filesystem::absolute(options.mmDirectory).string();
    if (!options.seedPath.empty()) {
        const std::filesystem::path seedPath = std::filesystem::absolute(options.seedPath);
        const std::filesystem::path directory = options.sessionDirectory.empty()
                                                    ? seedPath.parent_path() / (seedPath.stem().string() + ".fleet")
                                                    : std::filesystem::path(options.sessionDirectory);
        sSession.Open(seedPath, directory, options.slot, options.smokeItems || options.smokeCycle);
        if (!options.explicitStart) {
            options.start = sSession.ResumeGame() == "mm" ? Mm : Oot;
        }
    }
    CreateHostContext();
    OpenWindow(options);
    if (!options.internalRestore && (options.smokeFrames == 0 || options.testChoice >= 0)) {
        const int previous = Ship::Context::GetRawInstance()->GetHostConfig()->GetInt("Fleet.GameMode", 0);
        const UpdateFolders folders{ InstallFolder(), std::filesystem::absolute(options.ootDirectory),
                                     std::filesystem::absolute(options.mmDirectory) };
        sArchipelago = std::make_unique<ApSession>(ArchipelagoFolder(InstallFolder()));
        ApPanel archipelago(*sArchipelago, folders);
        options.mode = sFleetGui->ChooseGame(previous, folders, options.testChoice,
                                             options.testChoice < 0 ? &archipelago : nullptr);
        if (options.mode < 0) {
            return 0;
        }
        if (options.mode == ApPanel::kSetUpCombo) {
            options.mode = 0;
            sArchipelagoComboSlot = archipelago.Slot();
        }
        auto config = Ship::Context::GetRawInstance()->GetHostConfig();
        config->SetInt("Fleet.GameMode", options.mode);
        config->Save();
        options.start = options.mode == 2 ? Mm : Oot;
        // The archipelago mod registers its items as the games load, so the session must exist before they attach.
        if (sArchipelago->IsActive()) {
            sSession.Open(sArchipelago->SeedPath(), sArchipelago->SessionDirectory(), 0);
            options.start = sSession.ResumeGame() == "mm" ? Mm : Oot;
        } else {
            sArchipelago.reset();
        }
    }
    sMode = options.mode;
    if (sMode != 0) {
        options.start = sMode == 2 ? Mm : Oot;
    }
    sLobbyStart = options.start;
    if (!AttachGames(options)) {
        return 2;
    }
    if (sMode == 0 && !sSession.Enabled() && (sSmokeFrames == 0 || options.smokeNew >= 0)) {
        char* base = SDL_GetBasePath();
        const std::filesystem::path directory = base != nullptr ? base : ".";
        SDL_free(base);
        sSaveMenu = std::make_shared<FleetSaveMenu>(directory, std::filesystem::absolute(options.ootDirectory).string(),
                                                    std::filesystem::absolute(options.mmDirectory).string(),
                                                    options.savesDirectory);
        sSaveMenu->SetSnapshot([](const std::string& game) {
            return Ship::GameRegistry::Get(game) != nullptr
                       ? nlohmann::json::parse(sGames[IndexOf(game.c_str())].module->Request("DumpSettings"))
                       : nlohmann::json{};
        });
        sSaveMenu->SetIconSource(FindItemIcon);
        sSaveMenu->SetGameRequest([](const std::string& game, const std::string& op, const std::string& payload) {
            const auto& module = sGames[IndexOf(game.c_str())].module;
            return module != nullptr ? module->Request(op, payload) : std::string{};
        });
        if (options.smokeNew >= 0 && !options.smokeNativeGeneration) {
            sSaveMenu->Automate(options.smokeNew, options.smokeResume, !options.smokeName);
        }
    }
    sFleetGui->SetReset(ResetActiveGame);
    sFleetGui->SetGenerator(sSaveMenu);
    sFleetGui->SetItemTools(
        { .list = ListGivableItems,
          .give = [](const std::string& game, const std::string& item) { return sSession.GiveItem(game, item); },
          .findIcon = FindItemIcon,
          .giveAll = GiveAllItems,
          .drawTest = RunItemDrawTest });
    sFleetGui->SetNativeRequest([](const std::string& game, const std::string& op, const std::string& payload) {
        const auto& module = sGames[IndexOf(game.c_str())].module;
        return module ? module->Request(op, payload) : std::string("null");
    });
    RegisterCommands();
    if (sMode == 0) {
        sFleetGui->SetLoading("Preparing Fleet...");
    }
    GameFiber::EnterMainThread();
    Activate(options.start);
    return 0;
}

// The Context destructor would save the window, Fleet's config and each game's options, but EndProcess never runs it.
void SaveHostSettings() {
    Ship::Context* context = Ship::Context::GetRawInstance();
    if (context == nullptr) {
        return;
    }
    if (context->GetWindow() != nullptr) {
        context->GetWindow()->SaveWindowToConfig();
    }
    if (const auto config = context->GetHostConfig()) {
        config->Save();
    }
    for (const char* game : { "oot", "mm" }) {
        if (const auto services = Ship::GameRegistry::Get(game)) {
            Ship::GameScope scope(game);
            services->ConsoleVariables->Save();
        }
    }
}

// Parked fibers never unwind their native loops; process termination is their teardown.
void EndProcess(int status) {
    std::cout.flush();
    std::cerr.flush();
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(status));
#endif
    std::_Exit(status);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--internal-worker") {
        return FleetSeedWorker(argc - 1, argv + 1);
    }
    try {
#ifdef FLEET_TEST_DRIVER
        if (EnemyWalk::IsSupervisorCall(argc, argv)) {
            EndProcess(EnemyWalk::Supervise(argc, argv, InstallFolder()));
        }
#endif
        const int status = RunFleet(argc, argv);
        SaveHostSettings();
        EndProcess(status);
    } catch (const std::exception& error) {
        std::cerr << "[fleet] " << error.what() << "\n";
#if defined(_WIN32) && !defined(FLEET_TEST_DRIVER)
        MessageBoxA(nullptr, error.what(), "Fleet", MB_OK | MB_ICONERROR);
#endif
        EndProcess(2);
    }
}
