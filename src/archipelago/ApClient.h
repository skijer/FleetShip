#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

// Carries Archipelago packets on its own thread; the protocol itself runs on the caller's thread.
class ApClient {
  public:
    enum class Status { Idle, Connecting, Open, Closed };

    ~ApClient();
    void Start(const std::string& address);
    void Stop();
    void Send(const nlohmann::json& command);
    std::vector<nlohmann::json> TakeCommands();
    Status GetStatus() const;
    std::string GetError() const;

  private:
    void Run(std::string address);

    std::thread mThread;
    std::atomic<bool> mStopping = false;
    std::atomic<Status> mStatus = Status::Idle;
    mutable std::mutex mMutex;
    std::vector<std::string> mOutbox;
    std::vector<nlohmann::json> mInbox;
    std::string mError;
};
