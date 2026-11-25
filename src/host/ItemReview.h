#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

// A person judges every item as play shows it: Link holds it up with its textbox; A marks it correct, B wrong and
// Z goes back one. Each game also shows the other's items, as a check holding one would hand it over.
// Verdicts land after every item in a file shaped like the report's asset-review.json.
class ItemReview {
  public:
    enum class Choice { None, Keep, Reject, Back };

    struct Host {
        std::function<nlohmann::json(const std::string& game, const std::string& op, const nlohmann::json& input)>
            request;
        std::function<void(const std::string& from, const std::string& to)> travel;
        std::function<Choice()> readChoice;
        std::function<void(const std::string& game, const std::string& item, const std::string& text)> prompt;
    };

    struct Review {
        std::filesystem::path file;
        nlohmann::json items = nlohmann::json::object();
        std::map<std::string, std::string> labels;
    };

    struct ReviewItem {
        std::string owner;
        std::string item;
    };

    using GameItems = std::pair<std::string, std::vector<ReviewItem>>;

    // "game:item" for a game's own item, "game[owner]:item" for the other game's shown in it.
    static std::string KeyOf(const std::string& game, const ReviewItem& entry) {
        return entry.owner == game ? game + ":" + entry.item : game + "[" + entry.owner + "]:" + entry.item;
    }

    ItemReview(std::vector<GameItems> items, Review review, Host host)
        : mItems(std::move(items)), mReview(std::move(review)), mHost(std::move(host)) {
        size_t total = 0;
        for (const auto& [game, entries] : mItems) {
            total += entries.size();
        }
        std::cout << "[fleet] item review: " << total << " items into " << mReview.file.string() << std::endl;
    }

    bool IsFinished() const {
        return mStep == Step::Done;
    }

    int Failures() const {
        return mWrong;
    }

    void Advance(const std::string& active) {
        switch (mStep) {
            case Step::GoToGame:
                StartGameOnceReady(active);
                break;
            case Step::Give:
                GiveItem();
                break;
            case Step::Watch:
                WatchItem(active);
                break;
            case Step::Done:
                break;
            default:
                std::cout << "[fleet] item review: unknown step" << std::endl;
                mStep = Step::Done;
                break;
        }
    }

  private:
    enum class Step { GoToGame, Give, Watch, Done };

    static constexpr int kStartTimeoutFrames = 300;

    const std::string& Game() const {
        return mItems[mGame].first;
    }

    const ReviewItem& Item() const {
        return mItems[mGame].second[mItem];
    }

    std::string Key() const {
        return KeyOf(Game(), Item());
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
        const nlohmann::json status = mHost.request(Game(), "TestStatus", nlohmann::json::object());
        if (!status.value("playing", false) || status.value("transitioning", true)) {
            return;
        }
        mItem = 0;
        mStep = Step::Give;
    }

    void GiveItem() {
        if (mItem == mItems[mGame].second.size()) {
            mGame++;
            mStep = Step::GoToGame;
            return;
        }
        std::cout << "[fleet] review " << Key() << std::endl;
        SaveReview(Key());
        mHost.request(Game(), "TestGetItem", { { "item", Item().item }, { "owner", Item().owner } });
        SetTurbo(true);
        ShowPrompt();
        mChoice = Choice::None;
        mFrames = 0;
        mStep = Step::Watch;
    }

    // B sticks even when A closes the textbox after it; a finished show still waits for a first A or B.
    void WatchItem(const std::string& active) {
        const Choice choice = mHost.readChoice();
        if (choice == Choice::Reject || choice == Choice::Back || (choice == Choice::Keep && mChoice == Choice::None)) {
            mChoice = choice;
        }
        const nlohmann::json status =
            active == Game() ? mHost.request(Game(), "TestStatus", nlohmann::json::object()) : nlohmann::json::object();
        const std::string phase = ReadPhase(status);
        if (mIsTurbo && (status.value("msgMode", 0) != 0 || phase == "done" || phase == "unknown")) {
            SetTurbo(false);
        }
        if (phase == "unknown" || (phase == "queued" && ++mFrames > kStartTimeoutFrames)) {
            Judge("wrong", phase == "unknown" ? "the game does not know the item" : "the get-item never started");
            SetTurbo(false);
            Next();
            return;
        }
        if (phase != "done" || mChoice == Choice::None) {
            return;
        }
        if (mChoice == Choice::Back) {
            mItem = mItem > 0 ? mItem - 1 : 0;
            mStep = Step::Give;
            return;
        }
        const nlohmann::json earlier = mReview.items.value(Key(), nlohmann::json::object());
        const bool isWrong = mChoice == Choice::Reject;
        Judge(isWrong ? "wrong" : "correct", isWrong ? earlier.value("note", std::string{}) : "");
        Next();
    }

    // Link lifts the item in turbo; the textbox and the wait for a verdict run at normal speed.
    void SetTurbo(bool isOn) {
        mHost.request(Game(), "TestTurbo", { { "on", isOn } });
        mIsTurbo = isOn;
    }

    std::string ReadPhase(const nlohmann::json& status) const {
        const nlohmann::json show = status.value("getItem", nlohmann::json::object());
        return show.value("item", std::string{}) == Item().item ? show.value("phase", std::string{}) : "";
    }

    void Next() {
        mItem++;
        mStep = Step::Give;
    }

    static std::string TitleOf(const std::string& game) {
        return game == "oot" ? "Ocarina of Time" : "Majora's Mask";
    }

    void ShowPrompt() {
        const auto label = mReview.labels.find(Key());
        const std::string from = Item().owner == Game() ? "" : "  (from " + TitleOf(Item().owner) + ")";
        std::string text = TitleOf(Game()) + "  " + std::to_string(mItem + 1) + "/" +
                           std::to_string(mItems[mGame].second.size()) + "   " +
                           (label != mReview.labels.end() ? label->second : Item().item) + from;
        const nlohmann::json earlier = mReview.items.value(Key(), nlohmann::json::object());
        const std::string verdict = earlier.value("verdict", std::string{});
        const std::string note = earlier.value("note", std::string{});
        if (!verdict.empty()) {
            text += "\nEarlier: " + verdict + (note.empty() ? "" : " - " + note);
        }
        text += "\nA: correct     B: wrong     Z: back";
        mHost.prompt(Item().owner, Item().item, text);
    }

    void Judge(const std::string& verdict, const std::string& note) {
        mReview.items[Key()] = { { "verdict", verdict }, { "note", note } };
        SaveReview("");
        std::cout << "[fleet] review " << Key() << ": " << verdict << std::endl;
    }

    // "showing" names the item on screen, so a review the game crashed during can blame it next time.
    void SaveReview(const std::string& showing) const {
        nlohmann::json review = { { "source", "in-game review" }, { "items", mReview.items } };
        if (!showing.empty()) {
            review["showing"] = showing;
        }
        std::ofstream(mReview.file) << review.dump(1);
    }

    void Finish() {
        mHost.prompt("", "", "");
        SaveReview("");
        for (const auto& [game, entries] : mItems) {
            for (const auto& item : entries) {
                const nlohmann::json entry = mReview.items.value(KeyOf(game, item), nlohmann::json::object());
                mWrong += entry.value("verdict", std::string{}) == "wrong" ? 1 : 0;
            }
        }
        std::cout << "[fleet] item review: " << mWrong << " wrong, saved in " << mReview.file.string() << std::endl;
        mStep = Step::Done;
    }

    std::vector<GameItems> mItems;
    Review mReview;
    Host mHost;
    Step mStep = Step::GoToGame;
    size_t mGame = 0;
    size_t mItem = 0;
    int mFrames = 0;
    Choice mChoice = Choice::None;
    bool mIsTurbo = false;
    int mWrong = 0;
};
