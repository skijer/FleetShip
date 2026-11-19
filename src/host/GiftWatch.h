#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "SaveDiff.h"

class GiftWatch {
  public:
    struct Host {
        std::function<nlohmann::json()> readDeliveries;
        std::function<nlohmann::json()> readReceipts;
        std::function<nlohmann::json()> readSave;
    };

    explicit GiftWatch(Host host) : mHost(std::move(host)) {
        mSave = mHost.readSave();
        TakeNewDelivery();
    }

    nlohmann::json TakeNewDelivery() {
        nlohmann::json added;
        for (const auto& delivery : mHost.readDeliveries()) {
            if (mKnownDeliveries.insert(delivery.at("id").get<std::string>()).second) {
                added = delivery;
            }
        }
        return added;
    }

    bool IsReceived(const nlohmann::json& delivery) const {
        const std::string id = delivery.at("id");
        const nlohmann::json receipts = mHost.readReceipts();
        return std::find(receipts.begin(), receipts.end(), id) != receipts.end();
    }

    std::vector<std::string> ListUnreceived() const {
        const nlohmann::json receipts = mHost.readReceipts();
        std::vector<std::string> unreceived;
        for (const auto& delivery : mHost.readDeliveries()) {
            const std::string id = delivery.at("id");
            if (std::find(receipts.begin(), receipts.end(), id) == receipts.end()) {
                unreceived.push_back(id);
            }
        }
        return unreceived;
    }

    std::optional<std::vector<std::string>> TakeSaveChanges() {
        nlohmann::json save = mHost.readSave();
        if (save.is_null()) {
            return std::nullopt;
        }
        std::vector<std::string> changes = ListChanges(save);
        mSave = std::move(save);
        return changes;
    }

  private:
    static constexpr size_t kMostChanges = 40;

    std::vector<std::string> ListChanges(const nlohmann::json& save) const {
        if (mSave.is_null()) {
            return { "first save written" };
        }
        std::vector<std::string> changes;
        SaveDiff::CollectChanges(mSave, save, "", changes);
        if (changes.size() > kMostChanges) {
            const size_t hidden = changes.size() - kMostChanges;
            changes.resize(kMostChanges);
            changes.push_back("... and " + std::to_string(hidden) + " more");
        }
        return changes;
    }

    Host mHost;
    nlohmann::json mSave;
    std::unordered_set<std::string> mKnownDeliveries;
};
