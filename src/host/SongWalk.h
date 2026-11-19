#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <ship/config/ConsoleVariable.h>

class SongWalk {
  public:
    struct Host {
        std::function<std::shared_ptr<Ship::ConsoleVariable>(const std::string& game)> settings;
        std::function<nlohmann::json(const std::string& game)> status;
        std::function<void(const std::string& from, const std::string& to)> travel;
    };

    explicit SongWalk(const std::filesystem::path& logPath, Host host)
        : mLog(logPath), mHost(std::move(host)), mSteps(BuildSteps()) {
        for (const char* game : { "oot", "mm" }) {
            ClearTester(game);
        }
    }

    bool IsFinished() const {
        return mStep == mSteps.size();
    }

    int Failures() const {
        return mFailures;
    }

    void Advance(const std::string& game) {
        const Step& step = mSteps[mStep];
        if (!mStarted) {
            Start(game, step);
            return;
        }
        WatchStates();
        WatchActiveGame(game);
        const nlohmann::json status = mHost.status(step.arrival);
        const bool isLanded = game == step.arrival && status.value("playing", false) && HasSeenAll(step);
        const bool isOnTarget = !mTargetScene || status.value("scene", -1) == *mTargetScene;
        mLandedFrames = isLanded && isOnTarget ? mLandedFrames + 1 : 0;
        if (mLandedFrames >= kSettleFrames) {
            Finish(true, status);
        } else if (++mWaitedFrames > kStepTimeoutFrames) {
            Finish(false, status);
        }
    }

  private:
    struct Request {
        std::string game;
        std::string text;
    };

    struct Step {
        std::string name;
        std::string start;
        std::vector<Request> requests;
        std::string arrival;
        std::optional<int> scene;
        std::vector<Request> mustSee;
    };

    static constexpr const char* kTesterBlock = "gMods.ItemDropTester.Song";
    static constexpr const char* kRequest = "gMods.ItemDropTester.Song.Request";
    static constexpr const char* kState = "gMods.ItemDropTester.Song.State";
    static constexpr const char* kText = "gMods.ItemDropTester.Song.Text";
    static constexpr const char* kDebug = "gMods.ItemDropTester.Song.Debug";
    static constexpr int kSettleFrames = 40;
    static constexpr int kStartSettleFrames = 30;
    static constexpr int kStepTimeoutFrames = 2400;
    static constexpr int kOotSacredForestMeadow = 0x56;
    static constexpr int kOotTempleOfTime = 0x43;
    static constexpr int kOotLakeHylia = 0x57;
    static constexpr int kMmMilkRoad = 0x22;
    static constexpr int kSceneBefore = -2;

    static std::vector<Step> BuildSteps() {
        return {
            { "only the Milk Road owl in MM",
              "mm",
              { { "mm", "owls:32" } },
              "mm",
              std::nullopt,
              { { "mm", "armed" } } },
            { "minuet in MM",
              "mm",
              { { "mm", "warp:oot.song.minuet" } },
              "oot",
              kOotSacredForestMeadow,
              { { "mm", "asked" } } },
            { "soaring in OoT to Milk Road",
              "oot",
              { { "oot", "soaring:choose" } },
              "mm",
              kMmMilkRoad,
              { { "oot", "map" }, { "oot", "answered" } } },
            { "prelude in MM",
              "mm",
              { { "mm", "warp:oot.song.prelude" } },
              "oot",
              kOotTempleOfTime,
              { { "mm", "asked" } } },
            { "soaring in OoT, cancelled",
              "oot",
              { { "oot", "soaring:cancel" } },
              "oot",
              kSceneBefore,
              { { "oot", "map" }, { "oot", "closed" } } },
            { "no owls in MM", "mm", { { "mm", "owls:0" } }, "mm", std::nullopt, { { "mm", "armed" } } },
            { "serenade in MM",
              "mm",
              { { "mm", "warp:oot.song.serenade" } },
              "oot",
              kOotLakeHylia,
              { { "mm", "asked" } } },
            { "soaring in OoT without owls",
              "oot",
              { { "oot", "soaring:close" } },
              "oot",
              kSceneBefore,
              { { "oot", "refused" }, { "oot", "closed" } } },
        };
    }

    void Write(const nlohmann::json& line) {
        mLog << line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << std::endl;
    }

    void ClearTester(const std::string& game) {
        mHost.settings(game)->ClearBlock(kTesterBlock);
    }

    std::string ReadState(const std::string& game) {
        return mHost.settings(game)->GetString(kState, "");
    }

    void Start(const std::string& game, const Step& step) {
        if (game != step.start) {
            mHost.travel(game, step.start);
            return;
        }
        if (!mHost.status(game).value("playing", false) || ++mWaitedFrames < kStartSettleFrames) {
            return;
        }
        mRecord = { { "step", step.name } };
        for (const char* owner : { "oot", "mm" }) {
            ClearTester(owner);
            mRecord["sceneBefore"][owner] = mHost.status(owner).value("scene", -1);
            mLastStates[owner] = "";
        }
        for (const Request& request : step.requests) {
            mHost.settings(request.game)->SetString(kRequest, request.text.c_str());
        }
        mTargetScene = step.scene;
        if (step.scene == kSceneBefore) {
            mTargetScene = mRecord["sceneBefore"][step.arrival].get<int>();
        }
        mSeen.clear();
        mStarted = true;
        mWaitedFrames = 0;
        mLandedFrames = 0;
    }

    void WatchStates() {
        for (const char* game : { "oot", "mm" }) {
            const std::string state = ReadState(game);
            if (state.empty() || state == mLastStates[game]) {
                continue;
            }
            mLastStates[game] = state;
            mSeen.insert(std::string(game) + ":" + state);
            nlohmann::json event = { { "game", game }, { "state", state }, { "frame", mWaitedFrames } };
            const std::string text = mHost.settings(game)->GetString(kText, "");
            if (!text.empty()) {
                event["text"] = text;
            }
            mRecord["events"].push_back(event);
        }
    }

    void WatchActiveGame(const std::string& game) {
        const nlohmann::json status = mHost.status(game);
        const nlohmann::json active = { { "active", game },
                                        { "playing", status.value("playing", false) },
                                        { "scene", status.value("scene", -1) } };
        if (active == mLastActive) {
            return;
        }
        mLastActive = active;
        nlohmann::json event = active;
        event["frame"] = mWaitedFrames;
        mRecord["events"].push_back(event);
    }

    bool HasSeenAll(const Step& step) const {
        for (const Request& expected : step.mustSee) {
            if (!mSeen.contains(expected.game + ":" + expected.text)) {
                return false;
            }
        }
        return true;
    }

    void Finish(bool passed, const nlohmann::json& status) {
        const Step& step = mSteps[mStep];
        for (const char* game : { "oot", "mm" }) {
            const std::string debug = mHost.settings(game)->GetString(kDebug, "");
            if (!debug.empty()) {
                mRecord["debug"][game] = debug;
            }
        }
        mRecord["passed"] = passed;
        mRecord["arrival"] = { { "game", step.arrival }, { "scene", status.value("scene", -1) } };
        if (mTargetScene) {
            mRecord["expectedScene"] = *mTargetScene;
        }
        Write(mRecord);
        std::cout << "[fleet] song walk " << (passed ? "PASS" : "FAIL") << ": " << step.name << " -> " << mRecord.dump()
                  << std::endl;
        mFailures += passed ? 0 : 1;
        ++mStep;
        mStarted = false;
        mWaitedFrames = 0;
        if (IsFinished()) {
            for (const char* game : { "oot", "mm" }) {
                ClearTester(game);
            }
        }
    }

    std::ofstream mLog;
    Host mHost;
    std::vector<Step> mSteps;
    size_t mStep = 0;
    bool mStarted = false;
    int mWaitedFrames = 0;
    int mLandedFrames = 0;
    std::optional<int> mTargetScene;
    nlohmann::json mLastActive;
    int mFailures = 0;
    nlohmann::json mRecord;
    std::set<std::string> mSeen;
    std::map<std::string, std::string> mLastStates;
};
