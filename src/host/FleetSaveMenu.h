#pragma once

#include "PoolWorker.h"
#include "SeedJob.h"
#include "rando/ShareChoices.h"
#include "rando/SharedItems.h"
#include <fleet/FleetGameApi.h>
#include <fleet/rando/CrossPassages.h>
#include <ship/window/gui/GuiWindow.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <nlohmann/json.hpp>

struct DungeonItemRow;
struct EntranceRow;

class FleetSaveMenu : public Ship::GuiWindow {
  public:
    struct Ready {
        std::filesystem::path seed;
        std::filesystem::path session;
        int slot;
        bool fresh;
    };
    FleetSaveMenu(std::filesystem::path directory, std::string oot, std::string mm, std::filesystem::path saves = {});
    ~FleetSaveMenu() override;
    bool Enabled() const;
    bool OpenFiles();
    bool SelectNative(int slot);
    bool GenerateNative(int slot);
    int GenerationState() const;
    const std::string& SaveDirectory(const std::string& game) const;
    std::string SaveFile(const std::string& game, int slot, bool backup) const;
    bool Manage(const std::string& operation, int from, int to);
    bool HasSlot(int slot) const;
    bool IsConfiguring() const {
        return mActive;
    }
    void WarmPoolWorker();
    const std::filesystem::path& Root() const {
        return mRoot;
    }
    void SetSnapshot(std::function<nlohmann::json(const std::string&)> snapshot);
    using IconSource = std::function<bool(const std::string& game, const std::string& item, FleetItemIcon& icon)>;
    void SetIconSource(IconSource source);
    using GameRequest =
        std::function<std::string(const std::string& game, const std::string& op, const std::string& payload)>;
    void SetGameRequest(GameRequest request);
    void Poll();
    void Draw() override;
    void DrawElement() override;
    void DrawGeneralPage();
    void DrawSongPlacement();
    void DrawDungeonItemRows();
    void DrawKeyRingCount();
    void DrawCrossEntranceRows();
    void DrawStartingEquipment();
    void DrawItemPoolPage();
    void DrawPlandomizerPage();
    bool CloseConfiguration();
    std::optional<Ready> Pending() const;
    std::optional<Ready> Generated(int slot) const;
    std::optional<Ready> SeedFor(int slot);
    void Complete();
    void Fail(const std::string& error);
    void Automate(int slot, bool resume, bool directStart = true);
    std::optional<std::string> AddDroppedSeed(const std::filesystem::path& file);

  protected:
    void InitElement() override {
    }
    void UpdateElement() override {
    }

  private:
    using ItemKey = std::pair<std::string, std::string>;
    void LoadSlots();
    void Select(int slot);
    void Generate();
    std::filesystem::path SeedFolder() const;
    void RefreshSeedFiles();
    std::string StoreSeedFile(const std::filesystem::path& source, const std::string& name);
    void UseSeedFile(const std::filesystem::path& file);
    void DrawSeedChoice(const ImVec4& theme);
    void DrawGenerationProgress();
    void DrawGenerationResult();
    void DrawStartingGame(const ImVec4& theme);
    void DrawGoal(const ImVec4& theme);
    void DrawCrossEntrances(const ImVec4& theme);
    nlohmann::json ComposeSettings();
    nlohmann::json FleetBlock() const;
    std::vector<std::string> FleetArguments(const std::string& seed);
    std::vector<std::string> GenerationArguments(const std::filesystem::path& run, const std::string& seed);
    std::vector<std::string> BaseArguments() const;
    void KeepFile(const std::string& name, const nlohmann::json& json);
    void LoadCatalogue();
    void LoadLogicChoices();
    void KeepLogicChoices();
    void DrawDungeonItemRow(const DungeonItemRow& row);
    void DrawDungeonRewardPlacement();
    void DrawEntranceRow(const EntranceRow& row);
    void RememberPoolCoverage(const nlohmann::json& pairs);
    bool IsCoveredByPool(const std::string& game, const std::string& name) const;
    bool FindIcon(const std::string& game, const std::string& name, FleetItemIcon& icon) const;
    void ApplyEquipmentDefaults();
    bool IsMmStartingWith(const char* name) const;
    nlohmann::json GeneralChoices() const;
    void LoadGeneralChoices();
    void KeepGeneralChoices();
    struct WorkerFeed {
        std::string op;
        std::vector<std::string> ignoredKeys;
        std::string requested;
        std::string observed;
        std::chrono::steady_clock::time_point checkedAt;
        std::string error;
    };
    void PrepareWorkerPage(WorkerFeed& feed);
    void FeedWorker(WorkerFeed& feed);
    bool IsComputing(const WorkerFeed& feed) const;
    void DrawWorkerStatus(const WorkerFeed& feed, const char* idle);
    void PollWorker();
    void WarmWorker();
    void StopWorker();
    void ReceivePool(const nlohmann::json& reply);
    void SortPool();
    std::vector<ItemKey> GroupUpgradeChains();
    std::vector<ItemKey> GroupVariants();
    void ReceiveChecks(const nlohmann::json& reply);
    void RememberNames(const nlohmann::json& items);
    struct ShareRow {
        std::string label;
        std::string tooltip;
        int family;
        std::string pairKey;
        std::string iconGame;
        std::string iconItem;
    };
    void RebuildShareRows(const nlohmann::json& modPairs);
    ShareRow* FindHostRow(const ComboRando::FoldedPair& folded);
    void NameFoldedPairs(const nlohmann::json& modPairs);
    void TellTwinRowsApart();
    bool MatchesShareFilter(const ShareRow& row, const std::string& filter) const;
    bool IsShared(const ShareRow& row) const;
    void SetShared(const ShareRow& row, bool shared);
    void SetAllShared(bool shared);
    std::vector<std::string> PairRowKeys() const;
    void GenerateOncePoolArrives();
    void LoadShareChoices();
    void KeepShareChoices();
    void DrawShareBar(const ImVec4& theme);
    void DrawShareList(const ImVec4& theme);
    void DrawShareRow(const ShareRow& row);
    struct ItemCell {
        std::string game;
        std::string name;
        std::string caption;
        bool faded;
        bool edited;
        bool inPool;
    };
    void DrawItemIcon(const std::string& game, const std::string& name, float size, bool faded);
    void DrawItemCell(const ItemCell& cell);
    void DrawItemTooltip(const std::string& game, const std::string& name);
    void WrapCell();
    std::string DisplayName(const std::string& game, const std::string& name) const;
    bool MatchesItemFilter(const std::string& filter, const std::string& game, const std::string& name) const;
    void SortCells(std::vector<ItemCell>& cells) const;
    void DrawCellsByGame(const char* id, std::vector<ItemCell> cells);
    void DrawCellBox(const char* title, const std::vector<ItemCell>& cells, float width);
    void DrawPoolSummary(const ImVec4& theme);
    void DrawJunkCell(const std::vector<const nlohmann::json*>& junk, int balance, const std::string& filter);
    void DrawPoolCell(const ItemCell& cell);
    void DrawPoolCellMenu(const std::string& game, const std::string& name);
    void DrawPoolEditsBar(const ImVec4& theme);
    int PoolEdit(const std::string& game, const std::string& name) const;
    void ChangePoolEdit(const std::string& game, const std::string& name, int delta);
    nlohmann::json PoolEditsJson() const;
    void LoadPoolEdits();
    void LoadStartingItems();
    void LoadFleetStarting();
    void SaveFleetStarting() const;
    const nlohmann::json* NativeStarting(const std::string& game, const std::string& name) const;
    int StartingCount(const std::string& game, const std::string& name) const;
    int StartingCountAtPool(const std::string& game, const std::string& name) const;
    bool IsWithheldFromMmStart(const std::string& game, const std::string& name) const;
    int ShuffledCount(const std::string& game, const std::string& name) const;
    int PoolCount(const std::string& game, const std::string& name) const;
    int StartingMax(const std::string& game, const std::string& name) const;
    void SetStartingCount(const std::string& game, const std::string& name, int count);
    void ChangeStarting(const std::string& game, const std::string& name, int delta);
    void ClearStartingItems();
    void DrawStartingOptions();
    void DrawChosenStartingItems(const ImVec4& theme);
    void DrawUnshuffledStartingItems();
    void DrawStartingCell(const ItemCell& cell);
    struct PlandoCheck {
        std::string game;
        std::string name;
        std::string area;
        std::string vanilla;
    };
    struct PlandoItem {
        std::string game;
        std::string name;
        std::string label;
    };
    struct PlandoChoice {
        std::string state;
        std::string itemGame;
        std::string item;
    };
    nlohmann::json PlandoJson() const;
    void LoadPlando();
    void KeepPlando();
    void SetPlando(const PlandoCheck& check, std::optional<PlandoChoice> choice);
    void FilterChecks();
    void DrawPlandoFilters(const ImVec4& theme);
    void DrawPlandoBulk(const ImVec4& theme);
    void DrawPlandoTable();
    void DrawPlandoRow(const PlandoCheck& check);
    std::string DescribePlando(const PlandoCheck& check) const;
    void DrawItemPicker(const PlandoCheck& check);
    std::filesystem::path mDirectory;
    std::filesystem::path mRoot;
    std::filesystem::path mRun;
    std::string mOot;
    std::string mMm;
    std::map<std::string, std::string> mLobbyDirectories;
    SeedJob mJob;
    nlohmann::json mSlots = nlohmann::json::object();
    nlohmann::json mCatalogue;
    std::function<nlohmann::json(const std::string&)> mSnapshot;
    IconSource mIconSource;
    GameRequest mGameRequest;
    std::map<std::string, nlohmann::json> mStarting;
    nlohmann::json mFleetStarting = { { "oot", nlohmann::json::array() }, { "mm", nlohmann::json::array() } };
    std::map<std::string, nlohmann::json> mStartingAtPool;
    nlohmann::json mFleetStartingAtPool = nlohmann::json::object();
    std::map<std::string, nlohmann::json> mStartingRequested;
    nlohmann::json mFleetStartingRequested = nlohmann::json::object();
    bool mStartingStale = true;
    std::unique_ptr<PoolWorker> mPoolWorker;
    std::string mWorkerOp;
    WorkerFeed mPoolFeed = { "pool", { "poolEdits" } };
    WorkerFeed mChecksFeed = { "checks", { "poolEdits", "plando" } };
    nlohmann::json mPool = nlohmann::json::array();
    std::map<ItemKey, int> mPoolCopies;
    std::set<ItemKey> mCoveredByPool;
    std::map<std::string, std::set<std::string>> mPoolNamesByGame;
    std::map<ItemKey, std::string> mTwins;
    std::set<ItemKey> mSharedInPool;
    std::map<ItemKey, std::vector<std::string>> mUpgradeTiers;
    std::map<ItemKey, std::vector<std::string>> mVariants;
    std::map<ItemKey, std::string> mDisplayNames;
    std::map<ItemKey, std::string> mDescriptions;
    std::map<ItemKey, int> mPoolEdits;
    std::array<char, 64> mItemFilter = {};
    std::vector<PlandoCheck> mChecks;
    std::vector<PlandoItem> mPlandoItems;
    std::vector<std::string> mAreas;
    std::map<ItemKey, PlandoChoice> mPlando;
    std::vector<size_t> mShownChecks;
    std::string mShownChecksKey;
    int mChecksVersion = 0;
    std::array<char, 64> mCheckFilter = {};
    std::array<char, 64> mPickerFilter = {};
    int mAreaFilter = 0;
    int mGameFilter = 0;
    std::vector<std::string> mSeedFiles;
    int mSeedChoice = 0;
    std::optional<Ready> mReady;
    bool mEnabled = true;
    bool mActive = false;
    bool mSlotsValid = true;
    bool mLoadingCatalogue = false;
    bool mGenerating = false;
    bool mNativeRequested = false;
    bool mNativeMode = false;
    bool mSeedExplicit = false;
    bool mHunt = false;
    bool mEitherGame = false;
    bool mPlentifulModItems = false;
    nlohmann::json mKeptGeneral;
    int mRequired = 20;
    int mBossGoalPieces = 0;
    int mTotal = 30;
    int mStart = 0;
    int mSongs = 0;
    int mRewards = 0;
    int mKeyRingCount = 4;
    bool mEquipmentDefaultsApplied = false;
    std::array<bool, 7> mOwnGame = {};
    int mSlot = -1;
    int mAutomation = -1;
    bool mResumeTest = false;
    bool mDrawn = false;
    bool mPrepared = false;
    bool mStartRequested = false;
    bool mAutomated = false;
    nlohmann::json mResult;
    std::string mError;
    std::array<char, 16> mSeed = {};
    ComboRando::ShareChoices mShareChoices;
    std::vector<ShareRow> mShareRows;
    bool mGenerateWhenPoolArrives = false;
    std::array<char, 64> mShareFilter = {};
    std::array<bool, 8> mPools;
    std::array<bool, std::size(ComboRando::CrossDungeon::kPassages)> mPassages = {};
};
