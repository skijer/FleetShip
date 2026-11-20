#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <fleet/rando/CrossDungeonSeed.h>
#include <fleet/rando/CrossPassages.h>

// Walks every cross-game route of a seed: stand outside, go in, land in the other game, come back out and stay put.
class TransitionWalk {
  public:
    struct Host {
        std::function<nlohmann::json(const std::string& game, const std::string& op, const nlohmann::json& input)>
            request;
        std::function<void(const std::string& from, const std::string& to)> travel;
        std::function<bool(const std::filesystem::path&)> capture;
    };

    TransitionWalk(const nlohmann::json& crossSeed, const std::string& only, const std::filesystem::path& logPath,
                   Host host)
        : mSteps(BuildSteps(crossSeed)), mLog(logPath), mImages(logPath.parent_path() / "transition-walk"),
          mHost(std::move(host)) {
        std::erase_if(mSteps, [&](const Step& step) { return !IsSelected(step.name, only); });
        std::filesystem::create_directories(mImages);
        std::cout << "[fleet] transition walk: " << mSteps.size() << " routes into " << logPath.string() << std::endl;
    }

    bool IsFinished() const {
        return mStep == mSteps.size();
    }

    int Failures() const {
        return mFailures;
    }

    void Advance(const std::string& active) {
        Step& step = mSteps[mStep];
        Leg& leg = step.legs[mLeg];
        if (!mRequested) {
            Request(active, step, leg);
        } else if (mArrival.is_null()) {
            Watch(active, leg);
        } else {
            CheckFreedom(active, leg);
        }
    }

  private:
    static constexpr int kSettleFrames = 60;
    static constexpr int kFirstCheckFrames = 20;
    static constexpr int kLegTimeoutFrames = 1800;
    static constexpr int kNudgeFrames = 20;
    static constexpr int kAttemptFrames = 45;
    static constexpr float kMinimumStride = 15.0f;
    static constexpr int kStick = 60;
    static constexpr std::array<std::pair<int, int>, 4> kDirections = {
        { { 0, kStick }, { 0, -kStick }, { -kStick, 0 }, { kStick, 0 } }
    };

    struct Leg {
        std::string name;
        std::string game;
        nlohmann::json travel;
        std::string arrival;
        bool returnsToOrigin = false;
    };

    struct Step {
        std::string name;
        std::vector<Leg> legs;
    };

    // `only` lists name fragments separated by '|'; an empty list keeps every route.
    static bool IsSelected(const std::string& name, const std::string& only) {
        std::istringstream fragments(only);
        bool isEmpty = true;
        for (std::string fragment; std::getline(fragments, fragment, '|');) {
            isEmpty = false;
            if (name.find(fragment) != std::string::npos) {
                return true;
            }
        }
        return isEmpty;
    }

    static std::string GameOf(int game) {
        return game == 0 ? "oot" : "mm";
    }

    static nlohmann::json Travel(const char* mode, int32_t entrance) {
        return { { "mode", mode }, { "entrance", entrance } };
    }

    static std::vector<Step> BuildRouteSteps(const nlohmann::json& crossSeed) {
        using namespace ComboRando::CrossDungeon;
        std::vector<Dungeon> dungeons;
        for (const auto& entry : crossSeed.at("dungeons")) {
            dungeons.push_back(ParseDungeon(entry));
        }
        const auto& permutation = crossSeed.at("permutation");
        const bool isDecoupled = crossSeed.contains("exits");
        std::vector<Step> steps;
        for (size_t i = 0; i < dungeons.size(); i++) {
            const Dungeon& source = dungeons[i];
            const Dungeon& target = dungeons[permutation.at(i).get<size_t>()];
            if (source.pool == Pool::Boss || source.entrance.game == target.entrance.game) {
                continue;
            }
            const std::string outside = GameOf(source.entrance.game);
            const std::string inside = GameOf(target.entrance.game);
            steps.push_back({ source.id + " -> " + target.id,
                              { { "stand outside", outside, Travel("warp", source.returnTo.index), outside },
                                { "go in", outside, Travel("enter", source.entrance.index), inside },
                                { "come out", inside, Travel("exit", target.exit.index), outside, !isDecoupled } } });
        }
        return steps;
    }

    static std::vector<Step> BuildPassageSteps(const nlohmann::json& crossSeed) {
        using namespace ComboRando::CrossDungeon;
        std::vector<Step> steps;
        for (const auto& key : crossSeed.value("passages", nlohmann::json::array())) {
            const PassageDefinition* passage = FindPassage(key.get<std::string>());
            const nlohmann::json departure = { { "mode", "passage" }, { "passage", passage->key } };
            steps.push_back({ std::string("passage ") + passage->label,
                              { { "stand by the hole", "oot", Travel("warp", passage->oot), "oot" },
                                { "drop in", "oot", departure, "mm" },
                                { "drop back", "mm", departure, "oot", true } } });
        }
        return steps;
    }

    static std::vector<Step> BuildSteps(const nlohmann::json& crossSeed) {
        if (!crossSeed.is_object() || !crossSeed.value("enabled", false)) {
            return {};
        }
        std::vector<Step> steps = BuildRouteSteps(crossSeed);
        std::vector<Step> passages = BuildPassageSteps(crossSeed);
        steps.insert(steps.end(), passages.begin(), passages.end());
        return steps;
    }

    nlohmann::json Status(const std::string& game) {
        return mHost.request(game, "TestStatus", nlohmann::json::object());
    }

    void Request(const std::string& active, const Step& step, const Leg& leg) {
        if (active != leg.game) {
            mHost.travel(active, leg.game);
            return;
        }
        const nlohmann::json status = Status(active);
        if (!status.value("playing", false) || status.value("transitioning", true)) {
            return;
        }
        if (mLeg == 0) {
            mRecord = { { "step", step.name } };
        }
        mHost.request(leg.game, "TestTravel", leg.travel);
        mRequested = true;
        mWaitedFrames = 0;
        mStableFrames = 0;
        mLastPlace = nullptr;
    }

    void Watch(const std::string& active, const Leg& leg) {
        const nlohmann::json status = Status(active);
        const nlohmann::json place = { { "game", active },
                                       { "scene", status.value("scene", -1) },
                                       { "transitioning", status.value("transitioning", false) } };
        if (place != mLastPlace) {
            mLastPlace = place;
            mStableFrames = 0;
            nlohmann::json event = place;
            event["leg"] = leg.name;
            event["frame"] = mWaitedFrames;
            mRecord["events"].push_back(event);
        }
        const std::string travel = Status(leg.game).value("travel", "");
        if (travel.rfind("unsupported", 0) == 0 || travel.rfind("unknown", 0) == 0) {
            FinishStep(false, leg.name + ": " + travel);
            return;
        }
        const bool isSettling = ++mWaitedFrames > kFirstCheckFrames && active == leg.arrival &&
                                status.value("playing", false) && !status.value("transitioning", true);
        mStableFrames = isSettling ? mStableFrames + 1 : 0;
        if (mStableFrames >= kSettleFrames) {
            StartFreedomCheck(leg, status);
        } else if (mWaitedFrames > kLegTimeoutFrames) {
            FinishStep(false, leg.name + ": never settled in " + leg.arrival);
        }
    }

    std::string CaptureLeg(const std::string& moment) {
        const std::string image = std::to_string(mStep + 1) + "-" + std::to_string(mLeg + 1) + "-" + moment + ".ppm";
        return mHost.capture(mImages / image) ? image : "";
    }

    static float Stride(const nlohmann::json& from, const nlohmann::json& to) {
        if (!from.is_array() || !to.is_array()) {
            return 0.0f;
        }
        return std::hypot(to[0].get<float>() - from[0].get<float>(), to[2].get<float>() - from[2].get<float>());
    }

    void NudgeToward(const Leg& leg) {
        const auto [x, y] = kDirections[mAttempt];
        mHost.request(leg.arrival, "TestNudge", { { "frames", kNudgeFrames }, { "x", x }, { "y", y } });
        mAttemptFrames = 0;
    }

    // Link must stand on a floor where he landed, then walk away in some direction without being thrown back.
    void StartFreedomCheck(const Leg& leg, const nlohmann::json& status) {
        mArrival = status;
        mArrival["image"] = CaptureLeg("arrive");
        mAttempt = 0;
        mBestStride = 0.0f;
        mHasTouchedFloor = status.value("grounded", false);
        NudgeToward(leg);
    }

    void CheckFreedom(const std::string& active, const Leg& leg) {
        const nlohmann::json status = Status(active);
        const bool hasLeft = active != leg.arrival || status.value("transitioning", true) ||
                             status.value("scene", -1) != mArrival.value("scene", -1);
        const bool hasWalked = mBestStride >= kMinimumStride;
        if (hasLeft && !hasWalked) {
            FinishStep(false, leg.name + ": left " + leg.arrival + " scene " +
                                  std::to_string(mArrival.value("scene", -1)) + " right after arriving");
            return;
        }
        if (!hasLeft) {
            mBestStride = std::max(mBestStride, Stride(mArrival["player"], status["player"]));
            mHasTouchedFloor = mHasTouchedFloor || status.value("grounded", false);
        }
        const bool isAttemptOver = ++mAttemptFrames >= kAttemptFrames;
        if (!hasLeft && !hasWalked && isAttemptOver && mAttempt + 1 < kDirections.size()) {
            ++mAttempt;
            NudgeToward(leg);
            return;
        }
        if (hasLeft || hasWalked || isAttemptOver) {
            mHost.request(leg.arrival, "TestNudge", { { "frames", 0 } });
            FinishLeg(leg, { { "stride", mBestStride },
                             { "attempts", mAttempt + 1 },
                             { "walkedAway", hasLeft },
                             { "grounded", mHasTouchedFloor },
                             { "free", mArrival.value("free", false) },
                             { "arrive", mArrival.value("image", "") },
                             { "moved", CaptureLeg("moved") } });
        }
    }

    void FinishLeg(const Leg& leg, nlohmann::json freedom) {
        const int scene = mArrival.value("scene", -1);
        if (mLeg == 0) {
            mOriginScene = scene;
        }
        const bool isGrounded = freedom.value("grounded", false);
        const bool hasWalked = freedom.value("stride", 0.0f) >= kMinimumStride;
        mRecord["legs"].push_back(
            { { "leg", leg.name }, { "game", leg.arrival }, { "scene", scene }, { "freedom", std::move(freedom) } });
        mRequested = false;
        mArrival = nullptr;
        if (!isGrounded || !hasWalked) {
            FinishStep(false, leg.name + (isGrounded ? ": Link could not walk" : ": Link is not on a floor"));
            return;
        }
        if (leg.returnsToOrigin && scene != mOriginScene) {
            FinishStep(false, leg.name + ": returned to scene " + std::to_string(scene) + " instead of " +
                                  std::to_string(mOriginScene));
            return;
        }
        if (++mLeg == mSteps[mStep].legs.size()) {
            FinishStep(true, "");
        }
    }

    void FinishStep(bool passed, const std::string& reason) {
        mRecord["passed"] = passed;
        if (!reason.empty()) {
            mRecord["reason"] = reason;
        }
        mLog << mRecord.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << std::endl;
        std::cout << "[fleet] transition walk " << (passed ? "PASS" : "FAIL") << " " << mStep + 1 << "/"
                  << mSteps.size() << ": " << mSteps[mStep].name << (passed ? "" : " (" + reason + ")") << std::endl;
        mFailures += passed ? 0 : 1;
        ++mStep;
        mLeg = 0;
        mRequested = false;
        mArrival = nullptr;
    }

    std::vector<Step> mSteps;
    std::ofstream mLog;
    std::filesystem::path mImages;
    Host mHost;
    nlohmann::json mArrival;
    size_t mAttempt = 0;
    int mAttemptFrames = 0;
    float mBestStride = 0.0f;
    bool mHasTouchedFloor = false;
    size_t mStep = 0;
    size_t mLeg = 0;
    bool mRequested = false;
    int mWaitedFrames = 0;
    int mStableFrames = 0;
    int mOriginScene = -1;
    nlohmann::json mLastPlace;
    nlohmann::json mRecord;
    int mFailures = 0;
};
