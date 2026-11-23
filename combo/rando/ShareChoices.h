#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "CrossForeign.h"
#include "SharedItems.h"

namespace ComboRando {

// A folded pair rides with its host and a bottle pair with Shared Bottles, so neither gets a row of its own.
inline bool IsSharePairRow(const std::string& key, const nlohmann::json& modPairs) {
    const bool isBottlePair = std::find(std::begin(kBottlePairKeys), std::end(kBottlePairKeys), key) !=
                              std::end(kBottlePairKeys);
    if (isBottlePair) {
        return false;
    }
    const FoldedPair* folded = FindFoldedPair(key);
    if (folded == nullptr) {
        return true;
    }
    const bool hostPairFormed = std::any_of(modPairs.begin(), modPairs.end(), [&](const nlohmann::json& pair) {
        return pair.value("key", std::string{}) == folded->hostPair;
    });
    return folded->hostFamily == kNoSharedFamily && !hostPairFormed;
}

inline std::vector<std::string> ListSharePairRows(const nlohmann::json& modPairs) {
    std::vector<std::string> keys;
    for (const auto& pair : modPairs) {
        const std::string key = pair.value("key", std::string{});
        if (IsSharePairRow(key, modPairs)) {
            keys.push_back(key);
        }
    }
    return keys;
}

// What the player chose to share, as the launcher's Shared Items list shows it, and the generator arguments it
// becomes. Family choices live on the first family of each option; mod pairs are choices once the pool names them.
class ShareChoices {
  public:
    ShareChoices() {
        mFamilies.fill(true);
    }

    bool IsFamilyShared(int family) const {
        return mFamilies[family];
    }

    bool IsPairShared(const std::string& key) const {
        return !mUnsharedPairs.contains(key);
    }

    void SetFamilyShared(int family, bool shared) {
        mFamilies[family] = shared;
    }

    void SetPairShared(const std::string& key, bool shared) {
        if (shared) {
            mUnsharedPairs.erase(key);
        } else {
            mUnsharedPairs.insert(key);
        }
    }

    // Before the pool names the mod pairs the choice is remembered and reaches them in LearnPairRows.
    void SetAllShared(bool shared) {
        mUnsharedPairs.clear();
        for (int family = 0; family < SF_COUNT; family++) {
            if (IsFirstOfSharedOption(family)) {
                mFamilies[family] = shared;
            }
        }
        for (const std::string& key : mPairRows) {
            SetPairShared(key, shared);
        }
        if (!mPairsKnown) {
            mPendingBulkShare = shared;
        }
    }

    void LearnPairRows(std::vector<std::string> keys) {
        mPairRows = std::move(keys);
        mPairsKnown = true;
        if (!mPendingBulkShare) {
            return;
        }
        for (const std::string& key : mPairRows) {
            SetPairShared(key, *mPendingBulkShare);
        }
        mPendingBulkShare.reset();
    }

    bool IsBulkChoicePending() const {
        return mPendingBulkShare.has_value();
    }

    bool ArePairsKnown() const {
        return mPairsKnown;
    }

    std::vector<std::string> SharedOptionKeys() const {
        std::vector<std::string> keys;
        for (int family = 0; family < SF_COUNT; family++) {
            if (mFamilies[family] && IsFirstOfSharedOption(family)) {
                keys.push_back(SharedFamilyByIndex(family).key);
            }
        }
        return keys;
    }

    // Folded pairs follow their host row and bottle pairs follow Shared Bottles, as the generator expects.
    std::set<std::string> UnsharedPairs() const {
        std::set<std::string> unshared = mUnsharedPairs;
        for (const FoldedPair& folded : kFoldedPairs) {
            const std::optional<bool> hostShared = IsHostShared(folded);
            if (!hostShared) {
                continue;
            }
            if (*hostShared) {
                unshared.erase(folded.key);
            } else {
                unshared.insert(folded.key);
            }
        }
        const bool sharesBottles = IsFirstOfSharedOption(SF_BOTTLES) && mFamilies[SF_BOTTLES];
        for (const char* key : kBottlePairKeys) {
            if (sharesBottles) {
                unshared.erase(key);
            } else {
                unshared.insert(key);
            }
        }
        return unshared;
    }

    nlohmann::json ToJson() const {
        nlohmann::json unsharedFamilies = nlohmann::json::array();
        for (int family = 0; family < SF_COUNT; family++) {
            if (!mFamilies[family] && IsFirstOfSharedOption(family)) {
                unsharedFamilies.push_back(SharedFamilyByIndex(family).key);
            }
        }
        nlohmann::json json = { { "unsharedFamilies", unsharedFamilies }, { "unsharedPairs", mUnsharedPairs } };
        if (mPendingBulkShare) {
            json["pendingBulkShare"] = *mPendingBulkShare;
        }
        return json;
    }

    void ReadJson(const nlohmann::json& json) {
        if (!json.is_object()) {
            return;
        }
        const uint64_t unsharedFamilies =
            SharedMaskFromKeys(json.value("unsharedFamilies", nlohmann::json::array()));
        for (int family = 0; family < SF_COUNT; family++) {
            mFamilies[family] = (unsharedFamilies & (uint64_t{ 1 } << family)) == 0;
        }
        for (const auto& key : json.value("unsharedPairs", nlohmann::json::array())) {
            if (key.is_string()) {
                mUnsharedPairs.insert(key.get<std::string>());
            }
        }
        if (const auto pending = json.find("pendingBulkShare"); pending != json.end() && pending->is_boolean()) {
            mPendingBulkShare = pending->get<bool>();
        }
    }

  private:
    std::optional<bool> IsHostShared(const FoldedPair& folded) const {
        if (folded.hostFamily != kNoSharedFamily) {
            const int optionRow = SharedOptionFamily(SharedFamilyByIndex(folded.hostFamily).key);
            return mFamilies[optionRow];
        }
        const bool hostIsRow = std::find(mPairRows.begin(), mPairRows.end(), folded.hostPair) != mPairRows.end();
        return hostIsRow ? std::optional<bool>(IsPairShared(folded.hostPair)) : std::nullopt;
    }

    std::array<bool, SF_COUNT> mFamilies;
    std::set<std::string> mUnsharedPairs;
    std::vector<std::string> mPairRows;
    bool mPairsKnown = false;
    std::optional<bool> mPendingBulkShare;
};

} // namespace ComboRando
