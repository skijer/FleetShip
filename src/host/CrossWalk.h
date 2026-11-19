#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "GiftWatch.h"

class CrossWalk {
  public:
    struct Item {
        std::string game;
        std::string name;
    };

    struct Host {
        std::function<bool(const Item&)> give;
        std::function<void(const std::string& from, const std::string& to)> travel;
        std::function<GiftWatch::Host(const std::string& game)> watchGifts;
    };

    CrossWalk(std::vector<Item> items, std::vector<std::string> route, const std::filesystem::path& logPath, Host host)
        : mItems(std::move(items)), mRoute(std::move(route)), mLog(logPath), mGive(std::move(host.give)),
          mTravel(std::move(host.travel)) {
        for (const std::string& game : mRoute) {
            mGifts.try_emplace(game, host.watchGifts(game));
        }
    }

    bool IsFinished() const {
        return mStop == mRoute.size();
    }

    void Advance(const std::string& game) {
        const std::string& stop = mRoute[mStop];
        if (game != stop) {
            mTravel(game, stop);
            return;
        }
        if (!mHasArrived && !TryRecordArrival()) {
            return;
        }
        if (mPending && !TryRecordPending()) {
            return;
        }
        while (!mPending && GiveNextItem()) {}
        if (!mPending) {
            LeaveStop();
        }
    }

  private:
    static constexpr int kReceiptTimeoutFrames = 120;
    static constexpr int kArrivalTimeoutFrames = 600;

    void Write(const nlohmann::json& line) {
        mLog << line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << std::endl;
    }

    nlohmann::json DescribeStop() const {
        return { { "stop", mStop + 1 }, { "game", mRoute[mStop] } };
    }

    bool IsLastStop() const {
        return mStop + 1 == mRoute.size();
    }

    bool TryRecordArrival() {
        GiftWatch& gifts = mGifts.at(mRoute[mStop]);
        const std::vector<std::string> unreceived = gifts.ListUnreceived();
        const auto changes = unreceived.empty() ? gifts.TakeSaveChanges() : std::nullopt;
        const bool hasTimedOut = ++mWaitedFrames > kArrivalTimeoutFrames;
        if (!changes && !hasTimedOut) {
            return false;
        }
        nlohmann::json record = DescribeStop();
        record["arrived"] = true;
        record["unreceived"] = unreceived;
        if (changes) {
            record["changes"] = *changes;
        }
        Write(record);
        mHasArrived = true;
        mWaitedFrames = 0;
        return true;
    }

    bool GiveNextItem() {
        const std::string& stop = mRoute[mStop];
        while (mItem < mItems.size() && (IsLastStop() || mItems[mItem].game != stop)) {
            ++mItem;
        }
        if (mItem == mItems.size()) {
            return false;
        }
        const Item& item = mItems[mItem++];
        nlohmann::json record = DescribeStop();
        record["name"] = item.name;
        record["given"] = mGive(item);
        for (auto& [game, gifts] : mGifts) {
            const nlohmann::json delivery = gifts.TakeNewDelivery();
            if (!delivery.is_null()) {
                record[game == stop ? "delivery" : "twin"] = delivery;
            }
        }
        if (!record.contains("delivery")) {
            Write(record);
            return true;
        }
        mPending = std::move(record);
        mWaitedFrames = 0;
        return true;
    }

    bool TryRecordPending() {
        GiftWatch& gifts = mGifts.at(mRoute[mStop]);
        const bool isReceived = gifts.IsReceived(mPending->at("delivery"));
        const auto changes = isReceived ? gifts.TakeSaveChanges() : std::nullopt;
        const bool hasTimedOut = ++mWaitedFrames > kReceiptTimeoutFrames;
        if (!changes && !hasTimedOut) {
            return false;
        }
        nlohmann::json record = std::move(*mPending);
        mPending.reset();
        record["received"] = isReceived;
        if (changes) {
            record["changes"] = *changes;
        }
        Write(record);
        return true;
    }

    void LeaveStop() {
        ++mStop;
        mItem = 0;
        mHasArrived = false;
        mWaitedFrames = 0;
    }

    std::vector<Item> mItems;
    std::vector<std::string> mRoute;
    std::ofstream mLog;
    std::function<bool(const Item&)> mGive;
    std::function<void(const std::string&, const std::string&)> mTravel;
    std::map<std::string, GiftWatch> mGifts;
    size_t mStop = 0;
    size_t mItem = 0;
    bool mHasArrived = false;
    int mWaitedFrames = 0;
    std::optional<nlohmann::json> mPending;
};
