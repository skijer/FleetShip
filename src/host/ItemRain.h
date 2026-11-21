#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

// Per game: draws and announces every foreign item next to Link, checks custom C buttons (MM), rains waves of seed
// items around Link in the field, then lets the other game take its deliveries.
class ItemRain {
  public:
    struct Host {
        std::function<nlohmann::json(const std::string& game, const std::string& op, const nlohmann::json& input)>
            request;
        std::function<void(const std::string& from, const std::string& to)> travel;
        std::function<double()> slowestReportMilliseconds;
    };

    ItemRain(std::vector<std::string> games, int waves, double saveBudgetMilliseconds,
             const std::filesystem::path& logPath, Host host)
        : mGames(std::move(games)), mWaves(waves), mSaveBudgetMilliseconds(saveBudgetMilliseconds), mLog(logPath),
          mHost(std::move(host)) {
        std::cout << "[fleet] item rain: " << mWaves << " waves of " << kDropsPerWave << " in " << mGames.size()
                  << " games into " << logPath.string() << std::endl;
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
            case Phase::Showcase:
                RunShowcase(active, game);
                break;
            case Phase::Buttons:
                CheckButtons(active, game);
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
    enum class Phase { GoToGame, GoToField, Showcase, Buttons, Raining, Delivering };

    static constexpr int kDropsPerWave = 24;
    static constexpr int kWaveFrames = 150;
    static constexpr int kFieldFrames = 120;
    static constexpr int kParkedCatchUpFrames = 420;
    static constexpr int kDeliveryFrames = 240;
    static constexpr int kTimeoutFrames = 2400;
    static constexpr int kShowcaseFramesPerKey = 120;
    static constexpr int kButtonCheckFrames = 120;

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
            mPhase = Phase::Showcase;
            mFrames = 0;
        } else if (mFrames > kTimeoutFrames) {
            Finish(false, "never reached the field");
        }
    }

    // Covers a frame drawing the other game's item models (segment state) and its textbox icons (archive lookup).
    void RunShowcase(const std::string& active, const std::string& game) {
        if (active != game) {
            Finish(false, "left " + game + " during the foreign showcase");
            return;
        }
        if (mFrames++ == 0) {
            mHost.request(game, "TestForeignShowcase", nlohmann::json::object());
            return;
        }
        const nlohmann::json showcase = Status(game).value("showcase", nlohmann::json::object());
        const int timeout = kTimeoutFrames + showcase.value("keys", 0) * kShowcaseFramesPerKey;
        if (showcase.value("phase", "") == "done") {
            mRecord["showcase"] = showcase;
            mPhase = game == "mm" ? Phase::Buttons : Phase::Raining;
            mFrames = 0;
        } else if (mFrames > timeout) {
            mRecord["showcase"] = showcase;
            mProblems.push_back("foreign showcase never finished");
            mPhase = Phase::Raining;
            mFrames = 0;
        }
    }

    void CheckButtons(const std::string& active, const std::string& game) {
        if (mFrames++ == 0) {
            mHost.request(game, "TestCustomButtons", nlohmann::json::object());
            return;
        }
        const nlohmann::json status = Status(game);
        if (status.contains("buttons")) {
            mRecord["buttons"] = status.at("buttons");
            if (!status.at("buttons").value("passed", false)) {
                mProblems.push_back("custom C buttons lost or resurrected a key");
            }
        } else if (mFrames <= kButtonCheckFrames) {
            return;
        } else {
            mProblems.push_back("custom C button check never ran");
        }
        mPhase = Phase::Raining;
        mFrames = 0;
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

    // Each check saves the game and the host session on the frame it is collected, so these are frame stalls.
    void RecordTimings() {
        const double gameSave = std::max(mRecord["rain"].value("slowestSaveMs", 0.0),
                                         mRecord.value("other", nlohmann::json::object()).value("slowestSaveMs", 0.0));
        const double hostReport = mHost.slowestReportMilliseconds ? mHost.slowestReportMilliseconds() : 0.0;
        mRecord["timings"] = { { "slowestGameSaveMs", gameSave }, { "slowestHostReportMs", hostReport } };
        if (mSaveBudgetMilliseconds > 0.0 && std::max(gameSave, hostReport) > mSaveBudgetMilliseconds) {
            mProblems.push_back("a check stalled a frame for more than " + std::to_string(mSaveBudgetMilliseconds) +
                                " ms");
        }
    }

    void Finish(bool passed, const std::string& reason) {
        if (mRecord.contains("rain")) {
            RecordTimings();
        }
        if (!reason.empty()) {
            mProblems.insert(mProblems.begin(), reason);
        }
        passed = passed && mProblems.empty();
        mRecord["passed"] = passed;
        if (!mProblems.empty()) {
            std::string joined = mProblems.front();
            for (size_t index = 1; index < mProblems.size(); index++) {
                joined += "; " + mProblems[index];
            }
            mRecord["reason"] = joined;
        }
        mLog << mRecord.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << std::endl;
        std::cout << "[fleet] item rain " << (passed ? "PASS" : "FAIL") << ": " << mRecord.dump() << std::endl;
        mFailures += passed ? 0 : 1;
        ++mGame;
        mPhase = Phase::GoToGame;
        mFrames = 0;
        mDeliveryFrames = 0;
        mWave = 0;
        mProblems.clear();
    }

    std::vector<std::string> mGames;
    int mWaves;
    double mSaveBudgetMilliseconds;
    std::ofstream mLog;
    Host mHost;
    size_t mGame = 0;
    Phase mPhase = Phase::GoToGame;
    int mFrames = 0;
    int mDeliveryFrames = 0;
    int mWave = 0;
    nlohmann::json mRecord;
    std::vector<std::string> mProblems;
    int mFailures = 0;
};
