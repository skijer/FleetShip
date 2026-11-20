#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

// Rains waves of seed items around Link in each game's field, then lets the other game take its deliveries.
class ItemRain {
  public:
    struct Host {
        std::function<nlohmann::json(const std::string& game, const std::string& op, const nlohmann::json& input)>
            request;
        std::function<void(const std::string& from, const std::string& to)> travel;
    };

    ItemRain(std::vector<std::string> games, int waves, const std::filesystem::path& logPath, Host host)
        : mGames(std::move(games)), mWaves(waves), mLog(logPath), mHost(std::move(host)) {
        std::cout << "[fleet] item rain: " << mWaves << " waves of " << kDropsPerWave << " in "
                  << mGames.size() << " games into " << logPath.string() << std::endl;
    }

    bool IsFinished() const {
        return mGame == mGames.size();
    }

    int Failures() const {
        return mFailures;
    }

    void Advance(const std::string& active) {
        const std::string& game = mGames[mGame];
        switch (mPhase) {
            case Phase::GoToGame:
                GoTo(active, game, Phase::GoToField);
                break;
            case Phase::GoToField:
                StartField(active, game);
                break;
            case Phase::Raining:
                Rain(active, game);
                break;
            case Phase::Delivering:
                Deliver(active, game);
                break;
            default:
                Finish(false, "unknown phase");
                break;
        }
    }

  private:
    enum class Phase { GoToGame, GoToField, Raining, Delivering };

    static constexpr int kDropsPerWave = 24;
    static constexpr int kWaveFrames = 150;
    static constexpr int kFieldFrames = 120;
    static constexpr int kParkedCatchUpFrames = 420;
    static constexpr int kDeliveryFrames = 240;
    static constexpr int kTimeoutFrames = 2400;

    std::string OtherGame(const std::string& game) const {
        return game == "oot" ? "mm" : "oot";
    }

    nlohmann::json Status(const std::string& game) {
        return mHost.request(game, "TestStatus", nlohmann::json::object());
    }

    bool IsReady(const std::string& active, const std::string& game) {
        if (active != game) {
            return false;
        }
        const nlohmann::json status = Status(game);
        return status.value("playing", false) && !status.value("transitioning", true);
    }

    void GoTo(const std::string& active, const std::string& game, Phase next) {
        if (active != game) {
            mHost.travel(active, game);
            return;
        }
        if (IsReady(active, game)) {
            mPhase = next;
            mFrames = 0;
        }
    }

    void StartField(const std::string& active, const std::string& game) {
        if (mFrames == 0) {
            if (!IsReady(active, game)) {
                return;
            }
            mHost.request(game, "TestTravel", { { "mode", "field" } });
            mRecord = { { "game", game } };
        }
        if (++mFrames > kFieldFrames && IsReady(active, game)) {
            mRecord["field"] = Status(game).value("scene", -1);
            mPhase = Phase::Raining;
            mFrames = 0;
        } else if (mFrames > kTimeoutFrames) {
            Finish(false, "never reached the field");
        }
    }

    void Rain(const std::string& active, const std::string& game) {
        if (active != game) {
            Finish(false, "left " + game + " while it rained");
            return;
        }
        if (mFrames++ % kWaveFrames != 0) {
            return;
        }
        if (mWave == mWaves) {
            mPhase = Phase::Delivering;
            mFrames = 0;
            return;
        }
        const nlohmann::json status = mHost.request(game, "TestItemRain", { { "count", kDropsPerWave } });
        std::cout << "[fleet] item rain " << game << " wave " << mWave + 1 << "/" << mWaves << ": " << status.dump()
                  << std::endl;
        ++mWave;
    }

    void Deliver(const std::string& active, const std::string& game) {
        if (++mFrames < kParkedCatchUpFrames) {
            return;
        }
        const std::string other = OtherGame(game);
        if (!mRecord.contains("rain")) {
            mRecord["rain"] = Status(game);
        }
        if (mFrames > kParkedCatchUpFrames + kTimeoutFrames) {
            Finish(false, "never settled in " + other + " after the rain");
        } else if (active != other) {
            mHost.travel(active, other);
        } else if (++mDeliveryFrames > kDeliveryFrames && IsReady(active, other)) {
            mRecord["other"] = Status(other);
            const int collected = mRecord["rain"].value("collected", 0);
            Finish(collected > 0, collected > 0 ? "" : "no drop was collected");
        }
    }

    void Finish(bool passed, const std::string& reason) {
        mRecord["passed"] = passed;
        if (!reason.empty()) {
            mRecord["reason"] = reason;
        }
        mLog << mRecord.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << std::endl;
        std::cout << "[fleet] item rain " << (passed ? "PASS" : "FAIL") << ": " << mRecord.dump() << std::endl;
        mFailures += passed ? 0 : 1;
        ++mGame;
        mPhase = Phase::GoToGame;
        mFrames = 0;
        mDeliveryFrames = 0;
        mWave = 0;
    }

    std::vector<std::string> mGames;
    int mWaves;
    std::ofstream mLog;
    Host mHost;
    size_t mGame = 0;
    Phase mPhase = Phase::GoToGame;
    int mFrames = 0;
    int mDeliveryFrames = 0;
    int mWave = 0;
    nlohmann::json mRecord;
    int mFailures = 0;
};
