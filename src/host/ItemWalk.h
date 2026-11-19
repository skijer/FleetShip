#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "GiftWatch.h"

class ItemWalk {
  public:
    struct Item {
        std::string game;
        std::string name;
        std::string label;
    };

    struct Host {
        std::function<bool(const Item&)> give;
        GiftWatch::Host gifts;
    };

    ItemWalk(std::vector<Item> items, int copies, const std::filesystem::path& logPath, Host host)
        : mItems(std::move(items)), mCopies(copies), mLog(logPath), mGive(std::move(host.give)),
          mGifts(std::move(host.gifts)) {
    }

    bool IsFinished() const {
        return mItem == mItems.size() && !mPending;
    }

    void Advance() {
        if (mPending && !TryRecordPending()) {
            return;
        }
        while (mItem < mItems.size() && !mPending) {
            GiveNextCopy();
        }
    }

  private:
    static constexpr int kReceiptTimeoutFrames = 120;

    void Write(const nlohmann::json& line) {
        mLog << line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << std::endl;
    }

    void GiveNextCopy() {
        const Item& item = mItems[mItem];
        nlohmann::json record = {
            { "game", item.game }, { "name", item.name }, { "label", item.label }, { "copy", mCopy + 1 }
        };
        Write({ { "giving", item.game + ":" + item.name }, { "copy", mCopy + 1 } });
        record["given"] = mGive(item);
        if (++mCopy == mCopies) {
            mCopy = 0;
            ++mItem;
        }
        const nlohmann::json delivery = mGifts.TakeNewDelivery();
        if (delivery.is_null()) {
            Write(record);
            return;
        }
        record["delivery"] = delivery;
        mPending = std::move(record);
        mWaitedFrames = 0;
    }

    bool TryRecordPending() {
        const bool isReceived = mGifts.IsReceived(mPending->at("delivery"));
        const auto changes = isReceived ? mGifts.TakeSaveChanges() : std::nullopt;
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

    std::vector<Item> mItems;
    int mCopies;
    std::ofstream mLog;
    std::function<bool(const Item&)> mGive;
    GiftWatch mGifts;
    size_t mItem = 0;
    int mCopy = 0;
    int mWaitedFrames = 0;
    std::optional<nlohmann::json> mPending;
};
