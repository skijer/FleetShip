#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "rando/CrossForeign.h"
#include "rando/SharedItems.h"

// Gives one copy of every shareable item in one game at once (gifts skip the textboxes), then checks in the other
// game that each shared item arrived and is owned, and that each unshared one stayed home.
class ShareWalk {
  public:
    struct Host {
        std::function<nlohmann::json(const std::string& game, const std::string& op, const nlohmann::json& input)>
            request;
        std::function<void(const std::string& from, const std::string& to)> travel;
        std::function<void(const std::vector<std::pair<std::string, std::string>>& gifts)> give;
        std::function<nlohmann::json(const std::string& game)> deliveries;
        std::function<std::set<std::string>(const std::string& game)> items;
    };

    struct Expectation {
        std::string item;
        std::string twin;
        std::string key;
        bool shared;
        // A tier family crosses as a level in sharedTiers, never as a delivery.
        std::string tierKey;
    };

    ShareWalk(std::string source, const nlohmann::json& seed, const nlohmann::json& baseline,
              const std::filesystem::path& logPath, Host host)
        : mSource(std::move(source)), mTarget(mSource == "oot" ? "mm" : "oot"), mLog(logPath),
          mHost(std::move(host)) {
        const std::set<std::string> sourceItems = mHost.items(mSource);
        const std::set<std::string> targetItems = mHost.items(mTarget);
        for (Expectation& expectation : ListExpectations(mSource, seed, baseline)) {
            if (sourceItems.contains(expectation.item) && targetItems.contains(expectation.twin)) {
                mExpectations.push_back(std::move(expectation));
            } else {
                mSkipped++;
            }
        }
        for (const Expectation& expectation : mExpectations) {
            if (expectation.shared) {
                mSharedTwins.insert(expectation.twin);
            }
        }
        std::cout << "[fleet] share walk: " << mExpectations.size() << " items from " << mSource << " checked in "
                  << mTarget << " (" << mSkipped << " not in both games) into " << logPath.string() << std::endl;
    }

    bool IsFinished() const {
        return mPhase == Phase::Done;
    }

    int Failures() const {
        return mFailures;
    }

    void Advance(const std::string& active) {
        switch (mPhase) {
            case Phase::WaitSource:
                GiveOnceReady(active);
                break;
            case Phase::Settle:
                SettleInSource(active);
                break;
            case Phase::WaitTarget:
                CheckOnceDelivered(active);
                break;
            case Phase::Done:
                break;
            default:
                Finish("unknown phase");
                break;
        }
    }

    // The mod pairs the seed formed, families with an item in both games, and the baseline pairs it unshared.
    static std::vector<Expectation> ListExpectations(const std::string& source, const nlohmann::json& seed,
                                                     const nlohmann::json& baseline) {
        const bool fromOot = source == "oot";
        const uint64_t mask = ComboRando::SharedMaskFromKeys(seed.value("sharedItems", nlohmann::json::array()));
        std::vector<Expectation> expectations;
        std::set<std::string> listed;
        const auto add = [&](const std::string& oot, const std::string& mm, const std::string& key, bool shared,
                             const std::string& tierKey) {
            const std::string& item = fromOot ? oot : mm;
            const bool isBottle = ComboRando::FindBottleTwin(ComboRando::GAME_OOT, oot) != nullptr ||
                                  ComboRando::FindBottleTwin(ComboRando::GAME_MM, mm) != nullptr;
            if (item.empty() || isBottle || key == ComboRando::kTriforcePairKey || !listed.insert(item).second) {
                return;
            }
            expectations.push_back({ item, fromOot ? mm : oot, key, shared, tierKey });
        };
        // A formed pair decides its items before a family that names the same item.
        for (const auto& pair : seed.value("modFamilies", nlohmann::json::array())) {
            add(pair.value("oot", std::string{}), pair.value("mm", std::string{}), pair.value("key", std::string{}),
                true, "");
        }
        for (int family = 0; family < ComboRando::SF_COUNT; family++) {
            const auto& def = ComboRando::SharedFamilyByIndex(family);
            const bool isLiveState = (ComboRando::SharedLiveStateMask() & (uint64_t{ 1 } << family)) != 0;
            if (!def.mmHasItem || isLiveState || family == ComboRando::SF_BOTTLES) {
                continue;
            }
            const bool isTier = ComboRando::FindTierLadder(family) != nullptr;
            add(def.ootName, def.mmName, def.key, (mask & (uint64_t{ 1 } << family)) != 0,
                isTier ? ComboRando::SharedTierKey(family) : "");
        }
        std::set<std::string> unshared;
        for (const auto& key : seed.value("unsharedPairs", nlohmann::json::array())) {
            unshared.insert(key.get<std::string>());
        }
        for (const auto& pair : baseline.value("pairs", nlohmann::json::array())) {
            if (unshared.contains(pair.value("key", std::string{}))) {
                add(pair.value("oot", std::string{}), pair.value("mm", std::string{}), pair.value("key", ""), false,
                    "");
            }
        }
        return expectations;
    }

  private:
    enum class Phase { WaitSource, Settle, WaitTarget, Done };

    struct Outcome {
        bool delivered;
        bool received;
        bool ownedBefore;
        bool ownedAfter;
        int tierBefore;
        int tierAfter;
    };

    struct Observed {
        nlohmann::json owned = nlohmann::json::object();
        nlohmann::json tiers = nlohmann::json::object();
    };

    static constexpr int kSettleFrames = 120;
    static constexpr int kDeliveryFrames = 300;
    static constexpr int kTimeoutFrames = 3600;

    bool IsReady(const std::string& active, const std::string& game) {
        if (active != game) {
            return false;
        }
        const nlohmann::json status = mHost.request(game, "TestStatus", nlohmann::json::object());
        return status.value("playing", false) && !status.value("transitioning", true);
    }

    Observed ObserveTarget() {
        nlohmann::json twins = nlohmann::json::array();
        for (const Expectation& expectation : mExpectations) {
            twins.push_back(expectation.twin);
        }
        const nlohmann::json status = mHost.request(mTarget, "RuntimeStatus", nlohmann::json::object());
        return { mHost.request(mTarget, "TestOwns", { { "items", twins } }).value("owned", nlohmann::json::object()),
                 status.value("sharedTiers", nlohmann::json::object()) };
    }

    void GiveOnceReady(const std::string& active) {
        if (active != mSource) {
            mHost.travel(active, mSource);
            return;
        }
        if (!IsReady(active, mSource)) {
            return;
        }
        mBefore = ObserveTarget();
        std::vector<std::pair<std::string, std::string>> gifts;
        for (const Expectation& expectation : mExpectations) {
            gifts.emplace_back(mSource, expectation.item);
        }
        mHost.give(gifts);
        mPhase = Phase::Settle;
        mFrames = 0;
    }

    void SettleInSource(const std::string& active) {
        if (++mFrames < kSettleFrames) {
            return;
        }
        mHost.travel(active, mTarget);
        mPhase = Phase::WaitTarget;
        mFrames = 0;
    }

    std::string FindGiftId(const nlohmann::json& deliveries, const std::string& item) const {
        const std::string prefix = "give:" + mSource + ":";
        const std::string suffix = ":" + item;
        for (const auto& delivery : deliveries) {
            const std::string id = delivery.value("id", std::string{});
            const bool isThisGift = id.rfind(prefix, 0) == 0 && id.size() > suffix.size() &&
                                    id.compare(id.size() - suffix.size(), suffix.size(), suffix) == 0;
            if (isThisGift) {
                return id;
            }
        }
        return "";
    }

    void CheckOnceDelivered(const std::string& active) {
        if (++mFrames > kTimeoutFrames) {
            Finish("never settled in " + mTarget);
            return;
        }
        if (active != mTarget) {
            mHost.travel(active, mTarget);
            return;
        }
        if (mFrames < kDeliveryFrames || !IsReady(active, mTarget)) {
            return;
        }
        const nlohmann::json sourceGifts = mHost.deliveries(mSource);
        const nlohmann::json targetDeliveries = mHost.deliveries(mTarget);
        const nlohmann::json received =
            mHost.request(mTarget, "RuntimeStatus", nlohmann::json::object()).value("received", nlohmann::json());
        const Observed after = ObserveTarget();
        for (const Expectation& expectation : mExpectations) {
            Judge(expectation, FindGiftId(sourceGifts, expectation.item), targetDeliveries, received, after);
        }
        Finish("");
    }

    std::string FindProblem(const Expectation& expectation, const Outcome& outcome) const {
        if (!expectation.tierKey.empty()) {
            if (outcome.delivered) {
                return "a tier family was sent as an item";
            }
            if (expectation.shared && outcome.tierAfter <= outcome.tierBefore) {
                return "shared tier " + expectation.tierKey + " stayed at " + std::to_string(outcome.tierAfter);
            }
            if (!expectation.shared && outcome.tierAfter > outcome.tierBefore) {
                return "unshared tier " + expectation.tierKey + " rose in " + mTarget;
            }
            return "";
        }
        if (expectation.shared && !outcome.delivered) {
            return "shared but nothing was sent to " + mTarget;
        }
        if (expectation.shared && !outcome.received) {
            return mTarget + " never received it";
        }
        if (!expectation.shared && outcome.delivered) {
            return "unshared but it was sent to " + mTarget;
        }
        return "";
    }

    void Judge(const Expectation& expectation, const std::string& giftId, const nlohmann::json& targetDeliveries,
               const nlohmann::json& received, const Observed& after) {
        const bool delivered = !giftId.empty() && std::any_of(targetDeliveries.begin(), targetDeliveries.end(),
                                                              [&](const nlohmann::json& delivery) {
                                                                  return delivery.value("id", std::string{}) == giftId;
                                                              });
        const bool wasReceived =
            delivered && received.is_array() && std::find(received.begin(), received.end(), giftId) != received.end();
        const Outcome outcome = { delivered,
                                  wasReceived,
                                  mBefore.owned.value(expectation.twin, false),
                                  after.owned.value(expectation.twin, false),
                                  mBefore.tiers.value(expectation.tierKey, 0),
                                  after.tiers.value(expectation.tierKey, 0) };
        const std::string problem =
            giftId.empty() ? "the gift was not queued in " + mSource : FindProblem(expectation, outcome);
        // Owning is a separate signal: repeatable items (hearts, ammo) never read as owned, and another gift can
        // grant an unshared twin (a mask that teaches its moves).
        const bool ownershipUnconfirmed =
            problem.empty() && expectation.shared && expectation.tierKey.empty() && !outcome.ownedAfter;
        const bool ownedWithoutDelivery = !expectation.shared && !outcome.delivered && !outcome.ownedBefore &&
                                          outcome.ownedAfter && !mSharedTwins.contains(expectation.twin);
        const nlohmann::json record = { { "item", expectation.item },
                                        { "twin", expectation.twin },
                                        { "key", expectation.key },
                                        { "shared", expectation.shared },
                                        { "tier", expectation.tierKey },
                                        { "delivered", delivered },
                                        { "received", wasReceived },
                                        { "ownedBefore", outcome.ownedBefore },
                                        { "ownedAfter", outcome.ownedAfter },
                                        { "tierBefore", outcome.tierBefore },
                                        { "tierAfter", outcome.tierAfter },
                                        { "ownershipUnconfirmed", ownershipUnconfirmed },
                                        { "ownedWithoutDelivery", ownedWithoutDelivery },
                                        { "passed", problem.empty() },
                                        { "problem", problem } };
        mLog << record.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << std::endl;
        if (!problem.empty()) {
            mFailures++;
            std::cout << "[fleet] share walk FAIL " << expectation.item << " -> " << expectation.twin << ": "
                      << problem << std::endl;
        }
    }

    void Finish(const std::string& reason) {
        if (!reason.empty()) {
            mFailures++;
            std::cout << "[fleet] share walk aborted: " << reason << std::endl;
        }
        std::cout << "[fleet] share walk " << mSource << " -> " << mTarget << ": " << mExpectations.size()
                  << " items, " << mFailures << " failures" << std::endl;
        mPhase = Phase::Done;
    }

    std::string mSource;
    std::string mTarget;
    std::vector<Expectation> mExpectations;
    std::set<std::string> mSharedTwins;
    int mSkipped = 0;
    std::ofstream mLog;
    Host mHost;
    Phase mPhase = Phase::WaitSource;
    int mFrames = 0;
    Observed mBefore;
    int mFailures = 0;
};
