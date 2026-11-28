#include "ApClient.h"
#include "ApSocket.h"

namespace {
constexpr int kPollMilliseconds = 50;
}

ApClient::~ApClient() {
    Stop();
}

void ApClient::Start(const std::string& address) {
    Stop();
    {
        std::lock_guard lock(mMutex);
        mOutbox.clear();
        mInbox.clear();
        mError.clear();
    }
    mStopping = false;
    mStatus = Status::Connecting;
    mThread = std::thread(&ApClient::Run, this, address);
}

void ApClient::Stop() {
    mStopping = true;
    if (mThread.joinable()) {
        mThread.join();
    }
    mStatus = Status::Idle;
}

void ApClient::Send(const nlohmann::json& command) {
    std::lock_guard lock(mMutex);
    mOutbox.push_back(nlohmann::json::array({ command }).dump());
}

std::vector<nlohmann::json> ApClient::TakeCommands() {
    std::lock_guard lock(mMutex);
    return std::exchange(mInbox, {});
}

ApClient::Status ApClient::GetStatus() const {
    return mStatus;
}

std::string ApClient::GetError() const {
    std::lock_guard lock(mMutex);
    return mError;
}

void ApClient::Run(std::string address) {
    ApSocket socket;
    std::string error;
    if (!socket.Open(address, error)) {
        std::lock_guard lock(mMutex);
        mError = error;
        mStatus = Status::Closed;
        return;
    }
    mStatus = Status::Open;
    std::vector<std::string> messages;
    while (!mStopping) {
        std::vector<std::string> outgoing;
        {
            std::lock_guard lock(mMutex);
            outgoing.swap(mOutbox);
        }
        bool isAlive = true;
        for (const std::string& text : outgoing) {
            isAlive = isAlive && socket.SendText(text);
        }
        messages.clear();
        isAlive = isAlive && socket.Poll(kPollMilliseconds, messages);
        std::vector<nlohmann::json> commands;
        for (const std::string& message : messages) {
            const nlohmann::json packet = nlohmann::json::parse(message, nullptr, false);
            if (packet.is_array()) {
                commands.insert(commands.end(), packet.begin(), packet.end());
            }
        }
        if (!commands.empty()) {
            std::lock_guard lock(mMutex);
            mInbox.insert(mInbox.end(), commands.begin(), commands.end());
        }
        if (!isAlive) {
            std::lock_guard lock(mMutex);
            mError = "the Archipelago server closed the connection";
            mStatus = Status::Closed;
            return;
        }
    }
    socket.Close();
}
