#pragma once

#include "SeedJob.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class PoolWorker {
  public:
    PoolWorker(std::filesystem::path fleetExe, std::vector<std::string> baseArguments, std::filesystem::path folder);
    ~PoolWorker();
    PoolWorker(const PoolWorker&) = delete;
    PoolWorker& operator=(const PoolWorker&) = delete;
    void Request(const std::string& op, const std::filesystem::path& settingsFile,
                 const std::vector<std::string>& arguments);
    std::optional<nlohmann::json> TakeReply();
    bool IsBusy() const;
    bool IsWarm() const;
    void Stop();

  private:
    bool EnsureStarted();
    void SubmitQueued();
    void CollectReply();

    std::filesystem::path mFleetExe;
    std::vector<std::string> mBaseArguments;
    std::filesystem::path mFolder;
    SeedJob mProcess;
    std::optional<nlohmann::json> mQueued;
    std::optional<nlohmann::json> mReply;
    std::string mStartError;
    uint64_t mNextId = 1;
    uint64_t mPendingId = 0;
};
