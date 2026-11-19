#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <ship/config/ConsoleVariable.h>

#include "GiftWatch.h"

class DropWalk {
  public:
    struct Item {
        std::string name;
        std::string label;
    };

    struct Setting {
        std::string name;
        int32_t value;
    };

    struct Host {
        std::shared_ptr<Ship::ConsoleVariable> settings;
        std::vector<Setting> forcedSettings;
        std::function<bool(const std::filesystem::path&)> capture;
        std::function<bool()> requestSave;
        GiftWatch::Host gifts;
    };

    DropWalk(std::vector<Item> items, const std::filesystem::path& logPath, Host host)
        : mItems(std::move(items)), mImages(logPath.parent_path() / logPath.stem()), mLog(logPath),
          mSettings(std::move(host.settings)), mForcedSettings(std::move(host.forcedSettings)),
          mCapture(std::move(host.capture)), mRequestSave(std::move(host.requestSave)), mGifts(std::move(host.gifts)) {
        std::filesystem::remove_all(mImages);
        std::filesystem::create_directories(mImages);
        ForceSettings();
    }

    bool IsFinished() const {
        return mStep == Step::Finished;
    }

    void Advance() {
        switch (mStep) {
            case Step::Request:
                RequestNextItem();
                break;
            case Step::Pickup:
                WatchPickup();
                break;
            case Step::Capture:
                CapturePickup();
                break;
            case Step::Close:
                WatchClose();
                break;
            case Step::Save:
                WatchSave();
                break;
            case Step::Finished:
            default:
                break;
        }
    }

  private:
    enum class Step { Request, Pickup, Capture, Close, Save, Finished };

    static constexpr const char* kTesterSettings = "gMods.ItemDropTester.Auto";
    static constexpr const char* kRequest = "gMods.ItemDropTester.Auto.Request";
    static constexpr const char* kState = "gMods.ItemDropTester.Auto.State";
    static constexpr const char* kText = "gMods.ItemDropTester.Auto.Text";
    static constexpr const char* kIcon = "gMods.ItemDropTester.Auto.Icon";
    static constexpr const char* kRelease = "gMods.ItemDropTester.Auto.Release";
    static constexpr int kPickupTimeoutFrames = 1200;
    static constexpr int kCloseTimeoutFrames = 900;
    static constexpr int kReceiptTimeoutFrames = 240;
    static constexpr int kCaptureDelayFrames = 3;

    void Write(const nlohmann::json& line) {
        mLog << line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << std::endl;
    }

    void ForceSettings() {
        for (const Setting& setting : mForcedSettings) {
            const char* name = setting.name.c_str();
            const bool isSet = mSettings->Get(name) != nullptr;
            mPreviousSettings.push_back(isSet ? std::optional<int32_t>(mSettings->GetInteger(name, 0)) : std::nullopt);
            mSettings->SetInteger(name, setting.value);
        }
    }

    void RestoreSettings() {
        for (size_t i = 0; i < mForcedSettings.size(); ++i) {
            const char* name = mForcedSettings[i].name.c_str();
            if (mPreviousSettings[i]) {
                mSettings->SetInteger(name, *mPreviousSettings[i]);
            } else {
                mSettings->ClearVariable(name);
            }
        }
        mSettings->ClearBlock(kTesterSettings);
    }

    void Await(Step step) {
        mStep = step;
        mWaitedFrames = 0;
    }

    bool HasWaited(int frames) {
        return ++mWaitedFrames > frames;
    }

    void RecordItem() {
        Write(mRecord);
        Await(Step::Request);
    }

    void RequestNextItem() {
        if (mItem == mItems.size()) {
            RestoreSettings();
            mStep = Step::Finished;
            return;
        }
        const Item& item = mItems[mItem];
        ++mItem;
        Write({ { "dropping", item.name } });
        mRecord = { { "index", mItem }, { "name", item.name }, { "label", item.label } };
        mSettings->SetString(kState, "");
        mSettings->SetString(kRequest, item.name.c_str());
        Await(Step::Pickup);
    }

    bool IsRequestTaken() const {
        return std::string(mSettings->GetString(kRequest, "")).empty();
    }

    void WatchPickup() {
        const std::string state = IsRequestTaken() ? mSettings->GetString(kState, "") : "";
        const bool isRefused = state == "unknown" || state == "unspawnable" || state == "blocked";
        const bool isOver = state == "shown" || state == "silent";
        if (!isRefused && !isOver && !HasWaited(kPickupTimeoutFrames)) {
            return;
        }
        if (!IsRequestTaken()) {
            throw std::runtime_error("the drop tester mod never took the request: is it installed and is this a "
                                     "randomizer save?");
        }
        mRecord["pickup"] = isRefused || isOver ? state : "timeout after " + state;
        if (state == "blocked") {
            mRecord["blockedBy"] = mSettings->GetString(kText, "");
        }
        if (isRefused) {
            RecordItem();
            return;
        }
        Await(Step::Capture);
    }

    std::string NameImage() const {
        std::ostringstream name;
        name << std::setw(4) << std::setfill('0') << mItem << ".ppm";
        return name.str();
    }

    void CapturePickup() {
        if (!HasWaited(kCaptureDelayFrames)) {
            return;
        }
        const std::string image = NameImage();
        mRecord["text"] = mSettings->GetString(kText, "");
        mRecord["icon"] = mSettings->GetInteger(kIcon, -1);
        mRecord["image"] = mCapture(mImages / image) ? image : "";
        mSettings->SetInteger(kRelease, 1);
        Await(Step::Close);
    }

    void WatchClose() {
        const std::string state = mSettings->GetString(kState, "");
        const bool isClosed = state == "done" || state == "stuck";
        if (!isClosed && !HasWaited(kCloseTimeoutFrames)) {
            return;
        }
        mRecord["close"] = isClosed ? state : "timeout after " + state;
        mRecord["saveRequested"] = mRequestSave();
        mSaveTrigger = mGifts.TakeNewDelivery();
        if (mSaveTrigger.is_null()) {
            RecordItem();
            return;
        }
        Await(Step::Save);
    }

    void WatchSave() {
        const bool isReceived = mGifts.IsReceived(mSaveTrigger);
        const auto changes = isReceived ? mGifts.TakeSaveChanges() : std::nullopt;
        if (!changes && !HasWaited(kReceiptTimeoutFrames)) {
            return;
        }
        mRecord["saved"] = isReceived;
        if (changes) {
            mRecord["changes"] = *changes;
        }
        RecordItem();
    }

    std::vector<Item> mItems;
    std::filesystem::path mImages;
    std::ofstream mLog;
    std::shared_ptr<Ship::ConsoleVariable> mSettings;
    std::vector<Setting> mForcedSettings;
    std::vector<std::optional<int32_t>> mPreviousSettings;
    std::function<bool(const std::filesystem::path&)> mCapture;
    std::function<bool()> mRequestSave;
    GiftWatch mGifts;
    Step mStep = Step::Request;
    size_t mItem = 0;
    int mWaitedFrames = 0;
    nlohmann::json mRecord;
    nlohmann::json mSaveTrigger;
};
