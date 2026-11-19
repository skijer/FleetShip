#include "PoolWorker.h"

#include "../seedgen/ServeProtocol.h"

#include <iostream>
#include <system_error>

namespace {

void RemoveExchangeFiles(const std::filesystem::path& folder) {
    for (const char* file : { ServeProtocol::kRequestFile, ServeProtocol::kReplyFile, ServeProtocol::kReadyFile }) {
        std::error_code ignored;
        std::filesystem::remove(folder / file, ignored);
    }
}

} // namespace

PoolWorker::PoolWorker(std::filesystem::path fleetExe, std::vector<std::string> baseArguments,
                       std::filesystem::path folder)
    : mFleetExe(std::move(fleetExe)), mBaseArguments(std::move(baseArguments)), mFolder(std::move(folder)) {
    EnsureStarted();
}

PoolWorker::~PoolWorker() {
    Stop();
}

bool PoolWorker::EnsureStarted() {
    if (mProcess.Running()) {
        return true;
    }
    try {
        std::filesystem::create_directories(mFolder);
        RemoveExchangeFiles(mFolder);
        std::vector<std::string> arguments = mBaseArguments;
        arguments.insert(arguments.end(), { "--serve", mFolder.string() });
        mProcess.Start(mFleetExe, arguments, mFolder / "pool-worker.log");
        mStartError.clear();
        return true;
    } catch (const std::exception& error) {
        mStartError = error.what();
        std::cerr << "[fleet] pool worker: " << mStartError << "\n";
        return false;
    }
}

void PoolWorker::Request(const std::string& op, const std::filesystem::path& settingsFile,
                         const std::vector<std::string>& arguments) {
    mQueued = nlohmann::json{ { "op", op }, { "settings", settingsFile.string() }, { "arguments", arguments } };
    mReply.reset();
    SubmitQueued();
}

void PoolWorker::SubmitQueued() {
    if (!mQueued) {
        return;
    }
    if (mPendingId == 0) {
        mProcess.Poll();
    }
    if (!EnsureStarted()) {
        mReply = nlohmann::json{ { "error", mStartError } };
        mQueued.reset();
        return;
    }
    nlohmann::json request = *mQueued;
    request["id"] = mNextId;
    if (!ServeProtocol::TryWriteJson(mFolder / ServeProtocol::kRequestFile, request)) {
        return;
    }
    mPendingId = mNextId++;
    mQueued.reset();
}

void PoolWorker::CollectReply() {
    if (mPendingId == 0) {
        return;
    }
    if (const std::optional<int> status = mProcess.Poll()) {
        const std::string log = (mFolder / "pool-worker.log").string();
        mReply = nlohmann::json{ { "error",
                                   "Fleet's pool worker stopped (exit " + std::to_string(*status) + "); see " + log } };
        mPendingId = 0;
        return;
    }
    nlohmann::json reply = ServeProtocol::ReadJson(mFolder / ServeProtocol::kReplyFile);
    if (!reply.is_object() || reply.value("id", uint64_t{ 0 }) != mPendingId) {
        return;
    }
    reply.erase("id");
    mReply = std::move(reply);
    mPendingId = 0;
}

std::optional<nlohmann::json> PoolWorker::TakeReply() {
    SubmitQueued();
    CollectReply();
    std::optional<nlohmann::json> reply = std::move(mReply);
    mReply.reset();
    return reply;
}

bool PoolWorker::IsBusy() const {
    return mQueued.has_value() || mPendingId != 0;
}

bool PoolWorker::IsWarm() const {
    std::error_code ignored;
    return mProcess.Running() && std::filesystem::exists(mFolder / ServeProtocol::kReadyFile, ignored);
}

void PoolWorker::Stop() {
    mProcess.Cancel();
    mQueued.reset();
    mPendingId = 0;
}
