#pragma once

#include <atomic>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

class ComboSession {
  public:
    void Open(const std::filesystem::path& seedPath, const std::filesystem::path& directory, int slot,
              bool runtimeTests = false);
    bool Enabled() const;
    const std::string& Description() const;
    const std::string& SaveDirectory(const std::string& game) const;
    const std::string& Deliveries(const std::string& game) const;
    const std::string& SharedTiers(const std::string& game) const;
    bool ReportObtained(const std::string& game, const std::string& check);
    double SlowestReportMilliseconds() const;
    bool GiveItem(const std::string& game, const std::string& item);
    bool GiveItems(const std::vector<std::pair<std::string, std::string>>& items);
    nlohmann::json GivableItems() const;
    bool ReportSharedTiers(const std::string& game, const nlohmann::json& tiers);
    bool ReportBottles(const std::string& game, const std::string& bottles);
    std::string BottlesFor(const std::string& game) const;
    bool ReportLiveState(const std::string& game, const nlohmann::json& state);
    std::string LiveStateFor(const std::string& game) const;
    bool HasListedItems(const std::string& game) const;
    void SetListedItems(const std::string& game, std::set<std::string> names);
    bool ReportGoalReached(const std::string& game);
    const std::string& ResumeGame() const;
    void SetActiveGame(const std::string& game);
    void RequestCommit();
    bool IsCommitRequested() const;
    void CommitIfRequested();

  private:
    bool SharesBottles() const;
    std::optional<std::string> TwinName(const std::string& home, const std::string& item,
                                        const std::string& target) const;
    std::optional<std::string> MaskAlias(const std::string& game, const std::string& name) const;
    void QueueMaskAlias(nlohmann::json& queue, const std::string& game, const std::string& id,
                        const std::string& item) const;
    std::string DeliveredName(const std::string& target, const std::string& item) const;
    void RebuildDeliveries();
    void Commit() const;
    void RestoreCommitted() const;
    std::filesystem::path CommittedDirectory() const;
    void Persist(const std::set<std::string>& obtained, const nlohmann::json& tiers, const std::string& active,
                 const std::set<std::string>& cleared) const;

    nlohmann::json mSeed;
    std::filesystem::path mDirectory;
    std::string mDescription;
    std::map<std::string, std::string> mSaveDirectories;
    std::map<std::string, std::string> mDeliveries;
    std::set<std::string> mObtained;
    std::set<std::string> mClearedGames;
    nlohmann::json mTiers = nlohmann::json::object();
    nlohmann::json mBottles = nlohmann::json::object();
    nlohmann::json mLive = nlohmann::json::object();
    std::map<std::string, std::string> mSharedTiers;
    std::map<std::string, std::set<std::string>> mListedItems;
    std::string mActiveGame;
    std::atomic<bool> mCommitRequested = false;
    double mSlowestReportMilliseconds = 0.0;
};
