#include "ApSession.h"
#include "ApPaths.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <utility>

#include "host/ComboSession.h"
#include "seedgen/ArchipelagoSeed.h"

namespace {

constexpr int kItemsHandling = 0b101;
constexpr int kGoalStatus = 30;
constexpr double kRetrySeconds = 5.0;
constexpr size_t kMessageLimit = 50;
constexpr const char* kOotPrefix = "OoT: ";
constexpr const char* kMmPrefix = "MM: ";

double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

nlohmann::json ReadJson(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return stream ? nlohmann::json::parse(stream, nullptr, false) : nlohmann::json();
}

// "OoT: Hookshot" -> ("oot", "Hookshot").
std::pair<std::string, std::string> SplitItemName(const std::string& name) {
    for (const auto& [prefix, game] : { std::pair{ kOotPrefix, "oot" }, std::pair{ kMmPrefix, "mm" } }) {
        if (name.rfind(prefix, 0) == 0) {
            return { game, name.substr(std::strlen(prefix)) };
        }
    }
    return {};
}

} // namespace

ApSession::ApSession(std::filesystem::path folder) : mFolder(std::move(folder)) {
}

void ApSession::Connect(const ApConnectInfo& info) {
    mInfo = info;
    mOwnItemNames.clear();
    mOwnLocationNames.clear();
    mLocationIds.clear();
    mLocationGames.clear();
    mPendingItems.clear();
    mItems.clear();
    mStage = Stage::Connecting;
    mStatus = "Connecting to " + info.server + "...";
    mHasComposed = false;
    mIsGoalSent = false;
    mClient.Start(info.server);
}

ApSession::Stage ApSession::GetStage() const {
    return mStage;
}

const std::string& ApSession::Status() const {
    return mStatus;
}

bool ApSession::IsActive() const {
    return mStage == Stage::Ready;
}

const std::filesystem::path& ApSession::SeedPath() const {
    return mSeedPath;
}

const std::filesystem::path& ApSession::SessionDirectory() const {
    return mSessionDirectory;
}

const std::string& ApSession::Mode() const {
    return mMode;
}

void ApSession::Fail(const std::string& reason) {
    mClient.Stop();
    mStage = Stage::Failed;
    mStatus = reason;
}

void ApSession::Pump(ComboSession* session) {
    if (mStage == Stage::Idle || mStage == Stage::Failed) {
        return;
    }
    if (mClient.GetStatus() == ApClient::Status::Closed) {
        if (mStage != Stage::Ready) {
            Fail(mClient.GetError());
            return;
        }
        if (Now() >= mRetryAt) {
            mRetryAt = Now() + kRetrySeconds;
            mStatus = "Reconnecting to " + mInfo.server + "...";
            mClient.Start(mInfo.server);
        }
    }
    for (const nlohmann::json& command : mClient.TakeCommands()) {
        try {
            Handle(command, session);
        } catch (const std::exception& error) {
            Fail("Archipelago (" + command.value("cmd", std::string("?")) + "): " + error.what());
            return;
        }
    }
    DeliverPending(session);
}

void ApSession::Handle(const nlohmann::json& command, ComboSession* session) {
    const std::string name = command.value("cmd", std::string{});
    if (name == "RoomInfo") {
        HandleRoomInfo(command);
    } else if (name == "ConnectionRefused") {
        std::string reasons;
        for (const auto& error : command.value("errors", nlohmann::json::array())) {
            reasons += (reasons.empty() ? "" : ", ") + error.get<std::string>();
        }
        Fail("The server refused the slot" + (reasons.empty() ? std::string{} : " (" + reasons + ")"));
    } else if (name == "Connected") {
        HandleConnected(command, session);
    } else if (name == "DataPackage") {
        for (const auto& [game, package] : command.at("data").at("games").items()) {
            const nlohmann::json itemIds = package.value("item_name_to_id", nlohmann::json::object());
            for (const auto& [item, id] : itemIds.items()) {
                mItemNames[game][id.get<int64_t>()] = item;
            }
            const nlohmann::json locationIds = package.value("location_name_to_id", nlohmann::json::object());
            for (const auto& [location, id] : locationIds.items()) {
                mLocationNames[game][id.get<int64_t>()] = location;
            }
        }
    } else if (name == "LocationInfo") {
        HandleLocationInfo(command);
    } else if (name == "ReceivedItems") {
        HandleReceivedItems(command, session);
    } else if (name == "PrintJSON") {
        HandlePrint(command);
    }
}

void ApSession::HandleRoomInfo(const nlohmann::json& command) {
    mSeedName = command.value("seed_name", std::string("room"));
    mClient.Send({ { "cmd", "GetDataPackage" }, { "games", command.value("games", nlohmann::json::array()) } });
    mClient.Send({ { "cmd", "Connect" },
                   { "password", mInfo.password },
                   { "game", ComboRando::Archipelago::kGameName },
                   { "name", mInfo.slot },
                   { "uuid", Uuid() },
                   { "version", { { "major", 0 }, { "minor", 6 }, { "build", 4 }, { "class", "Version" } } },
                   { "items_handling", kItemsHandling },
                   { "tags", nlohmann::json::array() },
                   { "slot_data", true } });
}

void ApSession::HandleConnected(const nlohmann::json& command, ComboSession* session) {
    mSlot = command.at("slot").get<int>();
    for (const auto& player : command.value("players", nlohmann::json::array())) {
        mPlayerNames[player.value("slot", 0)] = player.value("alias", player.value("name", std::string{}));
    }
    const nlohmann::json slotInfo = command.value("slot_info", nlohmann::json::object());
    for (const auto& [slot, info] : slotInfo.items()) {
        mPlayerGames[std::stoi(slot)] = info.value("game", std::string{});
    }
    if (mHasComposed) {
        mStatus = "Connected to " + mInfo.server;
        SendObtainedChecks(session);
        if (mIsGoalSent) {
            mClient.Send({ { "cmd", "StatusUpdate" }, { "status", kGoalStatus } });
        }
        return;
    }
    LoadExport(command.at("slot_data").at("exportId").get<std::string>());
    if (mStage == Stage::Failed) {
        return;
    }
    nlohmann::json locations = nlohmann::json::array();
    for (const auto& [key, id] : mLocationIds) {
        locations.push_back(id);
    }
    mClient.Send({ { "cmd", "LocationScouts" }, { "locations", locations }, { "create_as_hint", 0 } });
    mStage = Stage::Preparing;
    mStatus = "Reading what the multiworld placed in your game...";
}

void ApSession::LoadExport(const std::string& exportId) {
    mExportFolder = mFolder / kArchipelagoExportsFolder / exportId;
    const nlohmann::json map = ReadJson(mExportFolder / kArchipelagoMapFile);
    const nlohmann::json names = ReadJson(mExportFolder / kArchipelagoNamesFile);
    if (!map.is_object() || !names.is_object()) {
        Fail("This slot was exported on another computer: copy its folder into " +
             ToUtf8(mFolder / kArchipelagoExportsFolder));
        return;
    }
    mMode = map.at("mode").get<std::string>();
    for (const auto& [name, id] : names.at("items").items()) {
        mOwnItemNames[id.get<int64_t>()] = name;
    }
    const nlohmann::json& locationIds = names.at("locations");
    for (const auto& [name, location] : map.at("locations").items()) {
        const int64_t id = locationIds.at(name).get<int64_t>();
        const std::string game = location.at("game");
        mLocationIds[game + ":" + location.at("check").get<std::string>()] = id;
        mOwnLocationNames[id] = name;
        mLocationGames[name] = game;
    }
}

void ApSession::HandleLocationInfo(const nlohmann::json& command) {
    if (!mHasComposed && mStage == Stage::Preparing) {
        ComposeSeed(command.at("locations"));
    }
}

void ApSession::ComposeSeed(const nlohmann::json& scouted) {
    std::vector<ComboRando::Archipelago::ScoutedItem> placed;
    std::set<std::pair<std::string, std::string>> registered;
    for (const auto& network : scouted) {
        const int64_t location = network.at("location").get<int64_t>();
        const auto name = mOwnLocationNames.find(location);
        if (name == mOwnLocationNames.end()) {
            continue;
        }
        ComboRando::Archipelago::ScoutedItem item = { name->second, network.at("item").get<int64_t>(),
                                                      network.at("player").get<int>(),
                                                      network.value("flags", 0) };
        if (item.player != mSlot) {
            const std::string key = ComboRando::Archipelago::ForeignKey(item.player, item.item);
            const std::string& game = mLocationGames.at(name->second);
            if (registered.insert({ game, key }).second) {
                const auto& keep = [this](std::string text) { return mStrings.emplace_back(std::move(text)).c_str(); };
                mItems[game].push_back({ keep(key), keep(ItemName(item.player, item.item)), keep(PlayerName(item.player)),
                                         keep(GameOf(item.player)), static_cast<uint32_t>(item.flags) });
            }
        }
        placed.push_back(std::move(item));
    }
    if (placed.size() != mOwnLocationNames.size()) {
        Fail("The multiworld holds " + std::to_string(placed.size()) + " of this export's " +
             std::to_string(mOwnLocationNames.size()) + " locations; generate it again from this export");
        return;
    }
    const nlohmann::json seed = FleetSeedgen_ComposeArchipelagoSeed(mExportFolder, placed, mSlot, mOwnItemNames);
    const std::string name = FileSafe(mSeedName) + "_" + FileSafe(mInfo.slot);
    mSessionDirectory = mFolder / "sessions" / name;
    mSeedPath = mFolder / "sessions" / (name + ".json");
    std::filesystem::create_directories(mSeedPath.parent_path());
    std::ofstream(mSeedPath, std::ios::binary | std::ios::trunc) << seed.dump(2);
    mHasComposed = true;
    mStage = Stage::Ready;
    mStatus = "Connected to " + mInfo.server;
}

void ApSession::HandleReceivedItems(const nlohmann::json& command, ComboSession* session) {
    const int64_t first = command.at("index").get<int64_t>();
    const nlohmann::json& items = command.at("items");
    for (size_t offset = 0; offset < items.size(); offset++) {
        const auto name = mOwnItemNames.find(items[offset].at("item").get<int64_t>());
        if (name != mOwnItemNames.end()) {
            mPendingItems[first + static_cast<int64_t>(offset)] = name->second;
        }
    }
    DeliverPending(session);
}

void ApSession::DeliverPending(ComboSession* session) {
    if (session == nullptr || !session->Enabled() || mPendingItems.empty()) {
        return;
    }
    std::vector<ComboSession::ReceivedItem> received;
    for (const auto& [index, name] : mPendingItems) {
        const auto [game, item] = SplitItemName(name);
        if (!game.empty()) {
            received.push_back({ index, game, item });
        }
    }
    if (session->ReceiveItems(received)) {
        mPendingItems.clear();
    }
}

void ApSession::SendObtainedChecks(ComboSession* session) {
    if (session == nullptr || !session->Enabled()) {
        return;
    }
    nlohmann::json locations = nlohmann::json::array();
    for (const auto& [game, check] : session->ObtainedChecks()) {
        const auto found = mLocationIds.find(game + ":" + check);
        if (found != mLocationIds.end()) {
            locations.push_back(found->second);
        }
    }
    if (!locations.empty()) {
        mClient.Send({ { "cmd", "LocationChecks" }, { "locations", locations } });
    }
}

void ApSession::ReportObtained(const std::string& game, const std::string& check) {
    const auto found = mLocationIds.find(game + ":" + check);
    if (mStage == Stage::Ready && found != mLocationIds.end()) {
        mClient.Send({ { "cmd", "LocationChecks" }, { "locations", { found->second } } });
    }
}

void ApSession::ReportGoal() {
    if (mStage != Stage::Ready) {
        return;
    }
    mIsGoalSent = true;
    mClient.Send({ { "cmd", "StatusUpdate" }, { "status", kGoalStatus } });
}

const FleetArchipelagoItem* ApSession::Items(const std::string& game, uint32_t* count) const {
    const auto found = mItems.find(game);
    if (found == mItems.end()) {
        *count = 0;
        return nullptr;
    }
    *count = static_cast<uint32_t>(found->second.size());
    return found->second.data();
}

void ApSession::HandlePrint(const nlohmann::json& command) {
    std::string text;
    for (const auto& part : command.value("data", nlohmann::json::array())) {
        const std::string type = part.value("type", std::string("text"));
        const std::string value = part.value("text", std::string{});
        const int64_t number = std::strtoll(value.c_str(), nullptr, 10);
        if (type == "player_id") {
            text += PlayerName(static_cast<int>(number));
        } else if (type == "item_id") {
            text += ItemName(part.value("player", 0), number);
        } else if (type == "location_id") {
            text += LocationName(part.value("player", 0), number);
        } else {
            text += value;
        }
    }
    if (text.empty()) {
        return;
    }
    mMessages.push_back(std::move(text));
    if (mMessages.size() > kMessageLimit) {
        mMessages.erase(mMessages.begin());
    }
}

std::vector<std::string> ApSession::TakeMessages() {
    return std::exchange(mMessages, {});
}

std::string ApSession::PlayerName(int slot) const {
    const auto found = mPlayerNames.find(slot);
    return found != mPlayerNames.end() ? found->second : "Player " + std::to_string(slot);
}

std::string ApSession::GameOf(int slot) const {
    const auto found = mPlayerGames.find(slot);
    return found != mPlayerGames.end() ? found->second : std::string("Archipelago");
}

std::string ApSession::ItemName(int slot, int64_t item) const {
    const auto game = mItemNames.find(GameOf(slot));
    if (game != mItemNames.end()) {
        if (const auto found = game->second.find(item); found != game->second.end()) {
            return found->second;
        }
    }
    return "an item";
}

std::string ApSession::LocationName(int slot, int64_t location) const {
    const auto game = mLocationNames.find(GameOf(slot));
    if (game != mLocationNames.end()) {
        if (const auto found = game->second.find(location); found != game->second.end()) {
            return found->second;
        }
    }
    return "somewhere";
}

std::string ApSession::Uuid() const {
    const std::filesystem::path path = mFolder / "uuid";
    std::string uuid;
    std::ifstream(path) >> uuid;
    if (!uuid.empty()) {
        return uuid;
    }
    std::mt19937_64 random{ std::random_device{}() };
    char text[33];
    std::snprintf(text, sizeof(text), "%016llx%016llx", static_cast<unsigned long long>(random()),
                  static_cast<unsigned long long>(random()));
    std::filesystem::create_directories(mFolder);
    std::ofstream(path) << text;
    return text;
}
