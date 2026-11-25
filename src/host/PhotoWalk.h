#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "AssetCatalogue.h"
#include "FrameCapture.h"

// Photographs every item's model as Fleet draws it: each game floats one item in front of its camera, the frame is
// captured and its middle kept. Games run in turbo while it lasts, so a photo costs a few frames.
class PhotoWalk {
  public:
    struct Host {
        std::function<nlohmann::json(const std::string& game, const std::string& op, const nlohmann::json& input)>
            request;
        std::function<void(const std::string& from, const std::string& to)> travel;
        std::function<void()> finish;
    };

    using GameItems = std::pair<std::string, std::vector<std::string>>;

    PhotoWalk(std::vector<GameItems> items, std::filesystem::path folder, Host host)
        : mItems(std::move(items)), mFolder(std::move(folder)), mHost(std::move(host)) {
        // A walk narrowed to a few items keeps the photos the earlier full walks took.
        const auto earlier = mFolder / "photos.json";
        if (std::filesystem::exists(earlier)) {
            mPhotos = nlohmann::json::parse(std::ifstream(earlier), nullptr, false).value("photos", mPhotos);
        }
        size_t total = 0;
        for (const auto& [game, names] : mItems) {
            total += names.size();
        }
        std::cout << "[fleet] photo walk: " << total << " items into " << mFolder.string() << std::endl;
    }

    bool IsFinished() const {
        return mStep == Step::Done;
    }

    int Failures() const {
        return mFailures;
    }

    void Advance(const std::string& active) {
        switch (mStep) {
            case Step::GoToGame:
                StartGameOnceReady(active);
                break;
            case Step::Request:
                RequestNextPhoto(active);
                break;
            case Step::Wait:
                CaptureOnceReady(active);
                break;
            case Step::Done:
                break;
            default:
                Fail("unknown step");
                mStep = Step::Done;
                break;
        }
    }

  private:
    enum class Step { GoToGame, Request, Wait, Done };

    static constexpr int kPhotoTimeoutFrames = 240;
    static constexpr int kMarkerMissesBeforeTrusting = 3;
    static constexpr uint32_t kSerialMask = 1023;
    static constexpr float kCropShare = 0.6f;

    const std::string& Game() const {
        return mItems[mGame].first;
    }

    const std::string& Item() const {
        return mItems[mGame].second[mItem];
    }

    bool IsReady(const std::string& active) {
        if (active != Game()) {
            return false;
        }
        const nlohmann::json status = mHost.request(Game(), "TestStatus", nlohmann::json::object());
        return status.value("playing", false) && !status.value("transitioning", true);
    }

    void StartGameOnceReady(const std::string& active) {
        if (mGame == mItems.size()) {
            Finish();
            return;
        }
        if (active != Game()) {
            mHost.travel(active, Game());
            return;
        }
        if (!IsReady(active)) {
            return;
        }
        mHost.request(Game(), "TestTurbo", { { "on", true } });
        mItem = 0;
        mStep = Step::Request;
    }

    void RequestNextPhoto(const std::string& active) {
        if (mItem == mItems[mGame].second.size()) {
            mHost.request(Game(), "TestItemPhoto", { { "item", "" } });
            mHost.request(Game(), "TestTurbo", { { "on", false } });
            mGame++;
            mStep = Step::GoToGame;
            return;
        }
        std::cout << "[fleet] photo " << Game() << ":" << Item() << std::endl;
        mSerial = (mSerial + 1) & kSerialMask;
        mHost.request(Game(), "TestItemPhoto", { { "owner", Game() }, { "item", Item() }, { "serial", mSerial } });
        mFrames = 0;
        mStep = Step::Wait;
    }

    void CaptureOnceReady(const std::string& active) {
        const nlohmann::json photo =
            mHost.request(Game(), "TestStatus", nlohmann::json::object()).value("photo", nlohmann::json::object());
        const bool isReady = active == Game() && photo.value("item", std::string{}) == Item() &&
                             photo.value("ready", false);
        const std::optional<FrameCapture::Frame> frame = isReady ? FrameCapture::Grab() : std::nullopt;
        const bool checksMarker = photo.value("marked", false) && mTrustsMarker;
        const bool showsThisRequest = frame && (!checksMarker || ReadMarker(*frame) == mSerial);
        if (!showsThisRequest) {
            if (++mFrames > kPhotoTimeoutFrames) {
                // Frames that are ready but never carry the marker mean it cannot be read here; stop asking for it.
                mMarkerMisses += frame ? 1 : 0;
                if (mTrustsMarker && mMarkerMisses >= kMarkerMissesBeforeTrusting) {
                    mTrustsMarker = false;
                    std::cout << "[fleet] photo walk: the frame marker is unreadable, photos are no longer checked"
                              << std::endl;
                }
                Fail(Game() + ":" + Item() + " never appeared" + (frame ? DescribeMarker(*frame) : ""));
                NextItem();
            }
            return;
        }
        mMarkerMisses = checksMarker ? 0 : mMarkerMisses;
        const AssetCatalogue::Image image = CropMiddle(*frame);
        const std::string file = "photos/" + Game() + "/" + AssetCatalogue::FileNameOf(Item()) + ".png";
        AssetCatalogue::WritePng(mFolder / file, image);
        mPhotos[Game() + ":" + Item()] = file;
        NextItem();
    }

    void NextItem() {
        mItem++;
        mStep = Step::Request;
    }

    // The host paints the request's serial as 5-bit red and green into the game's top-left corner, which sits
    // inside the 4:3 picture centred and scaled in the framebuffer.
    static uint32_t ReadMarker(const FrameCapture::Frame& frame) {
        constexpr float kGameWidth = 320.0f;
        constexpr float kGameHeight = 240.0f;
        constexpr float kMarkerMiddle = 4.0f;
        const float scale = frame.height / kGameHeight;
        const float left = (frame.width - kGameWidth * scale) / 2.0f;
        const uint32_t x = static_cast<uint32_t>(left + kMarkerMiddle * scale);
        const uint32_t y = static_cast<uint32_t>(kMarkerMiddle * scale);
        if (x >= frame.width || y >= frame.height) {
            return UINT32_MAX;
        }
        const char* pixel = frame.rgb.data() + (static_cast<size_t>(y) * frame.width + x) * 3;
        const uint32_t red = (static_cast<uint8_t>(pixel[0]) * 31 + 127) / 255;
        const uint32_t green = (static_cast<uint8_t>(pixel[1]) * 31 + 127) / 255;
        return red | (green << 5);
    }

    std::string DescribeMarker(const FrameCapture::Frame& frame) const {
        const auto pixel = [&](uint32_t x, uint32_t y) {
            const char* rgb = frame.rgb.data() + (static_cast<size_t>(y) * frame.width + x) * 3;
            return std::to_string(static_cast<uint8_t>(rgb[0])) + "," + std::to_string(static_cast<uint8_t>(rgb[1])) +
                   "," + std::to_string(static_cast<uint8_t>(rgb[2]));
        };
        return " (wanted marker " + std::to_string(mSerial) + ", read " + std::to_string(ReadMarker(frame)) + "; " +
               std::to_string(frame.width) + "x" + std::to_string(frame.height) + " top-left " + pixel(2, 2) +
               " bottom-left " + pixel(2, frame.height - 3) + ")";
    }

    static AssetCatalogue::Image CropMiddle(const FrameCapture::Frame& frame) {
        const uint32_t side = static_cast<uint32_t>(std::min(frame.width, frame.height) * kCropShare);
        const uint32_t left = (frame.width - side) / 2;
        const uint32_t top = (frame.height - side) / 2;
        AssetCatalogue::Image image = { side, side, {} };
        image.rgba.reserve(static_cast<size_t>(side) * side * 4);
        for (uint32_t y = 0; y < side; y++) {
            const uint32_t row = top + y;
            for (uint32_t x = 0; x < side; x++) {
                const char* pixel = frame.rgb.data() + (static_cast<size_t>(row) * frame.width + left + x) * 3;
                image.rgba.insert(image.rgba.end(), { static_cast<uint8_t>(pixel[0]), static_cast<uint8_t>(pixel[1]),
                                                      static_cast<uint8_t>(pixel[2]), 255 });
            }
        }
        return image;
    }

    void Fail(const std::string& reason) {
        mFailures++;
        mProblems.push_back(reason);
        std::cout << "[fleet] photo walk: " << reason << std::endl;
    }

    void Finish() {
        std::ofstream(mFolder / "photos.json") << nlohmann::json{ { "photos", mPhotos }, { "problems", mProblems } }.dump(1);
        if (mHost.finish) {
            mHost.finish();
        }
        std::cout << "[fleet] photo walk: " << mPhotos.size() << " photos, " << mFailures << " problems" << std::endl;
        mStep = Step::Done;
    }

    std::vector<GameItems> mItems;
    std::filesystem::path mFolder;
    Host mHost;
    Step mStep = Step::GoToGame;
    size_t mGame = 0;
    size_t mItem = 0;
    int mFrames = 0;
    uint32_t mSerial = 0;
    int mMarkerMisses = 0;
    bool mTrustsMarker = true;
    int mFailures = 0;
    nlohmann::json mPhotos = nlohmann::json::object();
    std::vector<std::string> mProblems;
};
