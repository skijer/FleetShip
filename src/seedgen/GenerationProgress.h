#pragma once
// Live generation status for the host's progress bar: only the writer thread touches progress.json once it starts.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "gui/ComboGenProgress.h"

enum class GenerationStage { Starting, Preparing, Entrances, Placing, Hints, Saving, Count };

struct GenerationStageBand {
    const char* title;
    const char* detail;
    float start;
    float end;
};

// Bands follow the timed test seeds: on a fully crossed seed the hint pare-down takes ~75 % and the fill ~18 %.
inline constexpr std::array<GenerationStageBand, static_cast<size_t>(GenerationStage::Count)> kGenerationStageBands = { {
    { "Starting both games", "Loading Ocarina of Time, Majora's Mask and every installed mod.", 0.00f, 0.03f },
    { "Preparing the worlds", "Reading every check, item and option of both games for this seed.", 0.03f, 0.05f },
    { "Connecting entrances",
      "Shuffling dungeons, bosses and interiors across both games until both logics accept the layout.", 0.05f,
      0.10f },
    { "Placing items", "Placing progression items where both games' logic can reach them.", 0.10f, 0.28f },
    { "Finding required items",
      "Replaying the seed without each progression item to learn which ones are required, for the hints.", 0.28f,
      0.98f },
    { "Saving the seed", "Writing the spoiler log and the seed file.", 0.98f, 1.00f },
} };

class GenerationProgress {
  public:
    explicit GenerationProgress(std::filesystem::path path) : mPath(std::move(path)) {
        Write();
        mWriter = std::thread([this] {
            while (!mIsStopping.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                Write();
            }
        });
    }

    // Apple's libc++ has no std::jthread, so the writer is stopped and joined by hand.
    ~GenerationProgress() {
        mIsStopping.store(true);
        mWriter.join();
    }

    GenerationProgress(const GenerationProgress&) = delete;
    GenerationProgress& operator=(const GenerationProgress&) = delete;

    void Enter(GenerationStage stage) {
        mCounters.placed.store(0);
        mCounters.total.store(0);
        mCounters.phase.store(0);
        mStage.store(stage);
    }

    void BeginAttempt(int attempt) {
        mAttempt.store(attempt);
    }

    ComboRando::ComboGenProgress* Counters() {
        return &mCounters;
    }

  private:
    static constexpr int kFillFinalizingPhase = 3;

    float StageFraction(GenerationStage stage) const {
        if (stage == GenerationStage::Placing && mCounters.phase.load() == kFillFinalizingPhase) {
            return 1.0f;
        }
        const int total = mCounters.total.load();
        if (total <= 0) {
            return 0.0f;
        }
        return std::clamp(static_cast<float>(mCounters.placed.load()) / static_cast<float>(total), 0.0f, 1.0f);
    }

    std::string DescribeCounters(GenerationStage stage) const {
        const int total = mCounters.total.load();
        if (total <= 0) {
            return {};
        }
        const int done = std::min(mCounters.placed.load(), total);
        const char* noun = stage == GenerationStage::Placing ? "placed" : "checked";
        return std::to_string(done) + " of " + std::to_string(total) + " progression items " + noun + ".";
    }

    nlohmann::json Describe() const {
        const GenerationStage stage = mStage.load();
        const GenerationStageBand& band = kGenerationStageBands[static_cast<size_t>(stage)];
        const bool isCounted = stage == GenerationStage::Placing || stage == GenerationStage::Hints;
        const float within = isCounted ? StageFraction(stage) : 0.0f;
        return { { "stage", static_cast<int>(stage) + 1 },
                 { "stages", static_cast<int>(GenerationStage::Count) },
                 { "title", band.title },
                 { "detail", band.detail },
                 { "count", isCounted ? DescribeCounters(stage) : std::string{} },
                 { "attempt", mAttempt.load() },
                 { "fraction", band.start + (band.end - band.start) * within } };
    }

    // Written aside and renamed so the host never parses a half-written file; a refused rename waits for the next tick.
    void Write() {
        const std::filesystem::path staged = mPath.string() + ".tmp";
        {
            std::ofstream file(staged, std::ios::trunc);
            file << Describe().dump();
        }
        std::error_code ignored;
        std::filesystem::rename(staged, mPath, ignored);
    }

    std::filesystem::path mPath;
    ComboRando::ComboGenProgress mCounters;
    std::atomic<GenerationStage> mStage{ GenerationStage::Starting };
    std::atomic<int> mAttempt{ 1 };
    std::atomic<bool> mIsStopping{ false };
    std::thread mWriter;
};
