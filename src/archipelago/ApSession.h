#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ApClient.h"

class ComboSession;

// Handed to the archipelago mod through the "ArchipelagoItems" host service; the mod copies nothing, so the
// strings stay owned here for the whole run.
struct FleetArchipelagoItem {
    const char* key;
    const char* item;
    const char* player;
    const char* game;
    uint32_t flags;
};

struct ApConnectInfo {
    std::string server;
    std::string slot;
    std::string password;
};

// Fleet's side of one Archipelago slot: prepares the session seed before the games start, then trades checks
// and items with the server while they run.
class ApSession {
  public:
    enum class Stage { Idle, Connecting, Preparing, Ready, Failed };

    explicit ApSession(std::filesystem::path folder);
    void Connect(const ApConnectInfo& info);
    void Pump(ComboSession* session);
    Stage GetStage() const;
    const std::string& Status() const;
    bool IsActive() const;
    const std::filesystem::path& SeedPath() const;
    const std::filesystem::path& SessionDirectory() const;
    const std::string& Mode() const;
    void ReportObtained(const std::string& game, const std::string& check);
    void ReportGoal();
    const FleetArchipelagoItem* Items(const std::string& game, uint32_t* count) const;
    std::vector<std::string> TakeMessages();

  private:
    void Fail(const std::string& reason);
    void Handle(const nlohmann::json& command, ComboSession* session);
    void HandleRoomInfo(const nlohmann::json& command);
    void HandleConnected(const nlohmann::json& command, ComboSession* session);
    void HandleLocationInfo(const nlohmann::json& command);
    void HandleReceivedItems(const nlohmann::json& command, ComboSession* session);
    void HandlePrint(const nlohmann::json& command);
    void LoadExport(const std::string& exportId);
    void ComposeSeed(const nlohmann::json& scouted);
    void DeliverPending(ComboSession* session);
    void SendObtainedChecks(ComboSession* session);
    std::string PlayerName(int slot) const;
    std::string GameOf(int slot) const;
    std::string ItemName(int slot, int64_t item) const;
    std::string LocationName(int slot, int64_t location) const;
    std::string Uuid() const;

    std::filesystem::path mFolder;
    ApClient mClient;
    ApConnectInfo mInfo;
    Stage mStage = Stage::Idle;
    std::string mStatus;
    std::string mSeedName;
    int mSlot = 0;
    bool mHasComposed = false;
    bool mIsGoalSent = false;
    double mRetryAt = 0;
    std::filesystem::path mExportFolder;
    std::filesystem::path mSeedPath;
    std::filesystem::path mSessionDirectory;
    std::string mMode;
    std::map<int, std::string> mPlayerNames;
    std::map<int, std::string> mPlayerGames;
    std::map<std::string, std::map<int64_t, std::string>> mItemNames;
    std::map<std::string, std::map<int64_t, std::string>> mLocationNames;
    std::map<int64_t, std::string> mOwnItemNames;
    std::map<int64_t, std::string> mOwnLocationNames;
    std::map<std::string, int64_t> mLocationIds;
    std::map<std::string, std::string> mLocationGames;
    std::map<int64_t, std::string> mPendingItems;
    std::map<std::string, std::vector<FleetArchipelagoItem>> mItems;
    std::deque<std::string> mStrings;
    std::vector<std::string> mMessages;
};
