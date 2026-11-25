#include "ComboSession.h"
#include <algorithm>
#include <chrono>
#include <cstring>

#include <fstream>
#include <map>
#include <utility>
#include <stdexcept>

#include "rando/CrossForeign.h"

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

void WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream << value.dump(2);
        stream.flush();
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("could not commit Fleet session file: " + path.string());
    }
#else
    std::filesystem::rename(temporary, path);
#endif
}

bool IsGame(const std::string& game) {
    return game == "oot" || game == "mm";
}

// RebuildDeliveries runs on every check; copying these seed sections per item made it grow with progress.
const nlohmann::json& SeedArray(const nlohmann::json& seed, const char* key) {
    static const nlohmann::json kEmpty = nlohmann::json::array();
    const auto found = seed.find(key);
    return found != seed.end() ? *found : kEmpty;
}

const nlohmann::json& SeedObject(const nlohmann::json& seed, const char* key) {
    static const nlohmann::json kEmpty = nlohmann::json::object();
    const auto found = seed.find(key);
    return found != seed.end() ? *found : kEmpty;
}

using ForeignIndex = std::map<std::pair<std::string, std::string>, const nlohmann::json*>;

ForeignIndex IndexForeignByCheck(const nlohmann::json& seed) {
    ForeignIndex index;
    for (const auto& foreign : seed.at("foreign")) {
        index.emplace(std::make_pair(foreign.at("checkGame").get<std::string>(),
                                     foreign.at("checkName").get<std::string>()),
                      &foreign);
    }
    return index;
}

} // namespace

void ComboSession::Open(const std::filesystem::path& seedPath, const std::filesystem::path& directory, int slot,
                        bool runtimeTests) {
    if (slot < 0 || slot > 2) {
        throw std::invalid_argument("Fleet slot must be 0, 1 or 2");
    }
    const nlohmann::json seed = nlohmann::json::parse(std::ifstream(seedPath));
    if (seed.value("fileType", "") != "ComboShipRandomizer") {
        throw std::invalid_argument("Fleet needs a combined randomizer spoiler");
    }
    for (const char* game : { "oot", "mm" }) {
        if (!seed.at(game).at("placements").is_object() || !seed.at(game).at("settings").is_object()) {
            throw std::invalid_argument("combined seed is missing placements or settings");
        }
        for (const auto& [check, item] : seed.at(game).at("placements").items()) {
            if (check.empty() || !item.is_string()) {
                throw std::invalid_argument("combined placements must name a check and an item");
            }
        }
    }
    std::set<std::string> foreignChecks;
    for (const auto& foreign : seed.at("foreign")) {
        const std::string source = foreign.at("checkGame");
        const std::string target = foreign.at("itemGame");
        if (!IsGame(source) || !IsGame(target) || !foreign.at("checkName").is_string() ||
            !foreign.at("itemName").is_string() ||
            !seed.at(source).at("placements").contains(foreign.at("checkName").get<std::string>())) {
            throw std::invalid_argument("invalid foreign placement in combined seed");
        }
        if (!foreignChecks.insert(source + ":" + foreign.at("checkName").get<std::string>()).second) {
            throw std::invalid_argument("combined seed repeats a foreign check");
        }
    }
    mDirectory = std::filesystem::absolute(directory).lexically_normal();
    const auto storedSeed = mDirectory / "seed.json";
    if (std::filesystem::exists(storedSeed) && nlohmann::json::parse(std::ifstream(storedSeed)) != seed) {
        throw std::invalid_argument("Fleet session belongs to a different seed; choose another --session folder");
    }
    std::filesystem::create_directories(mDirectory);
    WriteJson(storedSeed, seed);
    mSeed = seed;
    mDescription = nlohmann::json{ { "seed", seed }, { "slot", slot }, { "runtimeTests", runtimeTests } }.dump();
    for (const char* game : { "oot", "mm" }) {
        const auto folder = mDirectory / ("slot" + std::to_string(slot)) / game;
        std::filesystem::create_directories(folder);
        if (std::string(game) == "oot") {
            std::filesystem::create_directories(folder / "Save");
        }
        mSaveDirectories[game] = folder.string();
    }
    mDirectory /= "slot" + std::to_string(slot);
    RestoreCommitted();
    mObtained.clear();
    mClearedGames.clear();
    mListedItems.clear();
    mTiers = nlohmann::json::object();
    mBottles = nlohmann::json::object();
    mLive = nlohmann::json::object();
    mActiveGame = seed.value("startingGame", "OOT") == "MM" ? "mm" : "oot";
    const auto statePath = mDirectory / "obtained.json";
    if (std::filesystem::exists(statePath)) {
        const auto state = nlohmann::json::parse(std::ifstream(statePath));
        mObtained = state.at("obtained").get<std::set<std::string>>();
        mClearedGames = state.value("clearedGames", std::set<std::string>{});
        mTiers = state.value("sharedTiers", nlohmann::json::object());
        mBottles = state.value("bottles", nlohmann::json::object());
        mLive = state.value("live", nlohmann::json::object());
        mActiveGame = state.value("activeGame", mActiveGame);
        if (!IsGame(mActiveGame)) {
            throw std::invalid_argument("invalid active game in Fleet session");
        }
    }
    RebuildDeliveries();
}

std::filesystem::path ComboSession::CommittedDirectory() const {
    return mDirectory / "committed";
}

void ComboSession::Close() {
    if (!Enabled()) {
        return;
    }
    CommitIfRequested();
    RestoreCommitted();
    mSeed = nlohmann::json();
    mDirectory.clear();
    mDescription.clear();
    mSaveDirectories.clear();
    mDeliveries.clear();
    mObtained.clear();
    mClearedGames.clear();
    mTiers = nlohmann::json::object();
    mBottles = nlohmann::json::object();
    mLive = nlohmann::json::object();
    mSharedTiers.clear();
    mListedItems.clear();
    mActiveGame.clear();
    mCommitRequested = false;
}

void ComboSession::RequestCommit() {
    mCommitRequested = true;
}

bool ComboSession::IsCommitRequested() const {
    return mCommitRequested;
}

void ComboSession::CommitIfRequested() {
    if (Enabled() && mCommitRequested.exchange(false)) {
        Commit();
    }
}

void ComboSession::Commit() const {
    const auto committed = CommittedDirectory();
    const std::filesystem::path staging = committed.string() + ".tmp";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    for (const char* game : { "oot", "mm" }) {
        std::filesystem::copy(mDirectory / game, staging / game, std::filesystem::copy_options::recursive);
    }
    if (std::filesystem::exists(mDirectory / "obtained.json")) {
        std::filesystem::copy_file(mDirectory / "obtained.json", staging / "obtained.json");
    }
    std::filesystem::remove_all(committed);
    std::filesystem::rename(staging, committed);
}

void ComboSession::RestoreCommitted() const {
    const auto committed = CommittedDirectory();
    // Open runs first, before any play: a never-saved slot's starting point becomes what a reset returns to.
    if (!std::filesystem::exists(committed)) {
        Commit();
        return;
    }
    for (const char* game : { "oot", "mm" }) {
        std::filesystem::remove_all(mDirectory / game);
        std::filesystem::copy(committed / game, mDirectory / game, std::filesystem::copy_options::recursive);
    }
    std::filesystem::remove(mDirectory / "obtained.json");
    if (std::filesystem::exists(committed / "obtained.json")) {
        std::filesystem::copy_file(committed / "obtained.json", mDirectory / "obtained.json");
    }
}

bool ComboSession::Enabled() const {
    return !mDescription.empty();
}

const std::string& ComboSession::Description() const {
    return mDescription;
}

const std::string& ComboSession::SaveDirectory(const std::string& game) const {
    return mSaveDirectories.at(game);
}

const std::string& ComboSession::Deliveries(const std::string& game) const {
    return mDeliveries.at(game);
}

const std::string& ComboSession::ResumeGame() const {
    return mActiveGame;
}

void ComboSession::SetActiveGame(const std::string& game) {
    if (!IsGame(game)) {
        throw std::invalid_argument("unknown Fleet game");
    }
    if (game != mActiveGame) {
        Persist(mObtained, mTiers, game, mClearedGames);
        mActiveGame = game;
    }
}

const std::string& ComboSession::SharedTiers(const std::string& game) const {
    return mSharedTiers.at(game);
}

namespace {
bool IsPairReadAsOwned(const nlohmann::json& pair) {
    const std::string key = pair.value("key", "");
    const bool isBottle = key.rfind("fleet.bottle.", 0) == 0;
    const bool isCounted =
        key == "fleet.triforce_piece" || key == "nei.heart.golden" || key == "nei.heart.golden_piece";
    return !key.empty() && !isBottle && !isCounted && ComboRando::FindFoldedPair(key) == nullptr;
}
} // namespace

bool ComboSession::ReportSharedTiers(const std::string& game, const nlohmann::json& tiers) {
    if (!Enabled() || !IsGame(game) || !tiers.is_object()) {
        return false;
    }
    const uint64_t mask = ComboRando::SharedMaskFromKeys(mSeed.value("sharedItems", nlohmann::json::array()));
    auto next = mTiers;
    for (int family = 0; family < ComboRando::SF_COUNT; family++) {
        const auto& definition = ComboRando::SharedFamilyByIndex(family);
        const std::string tierKey = ComboRando::SharedTierKey(family);
        const auto found = tiers.find(tierKey);
        if ((mask & (uint64_t{ 1 } << family)) == 0 || (game == "mm" && definition.ootToMmOnly) ||
            found == tiers.end() || !found->is_number_integer()) {
            continue;
        }
        const int tier = found->get<int>();
        constexpr int kChainLevelLimit = 16;
        if (tier < 0 || tier > std::max(kChainLevelLimit, definition.mmTierCap)) {
            return false;
        }
        next[tierKey] = std::max(next.value(tierKey, 0), tier);
    }
    for (const auto& pair : mSeed.value("modFamilies", nlohmann::json::array())) {
        const auto found = IsPairReadAsOwned(pair) ? tiers.find(pair.at("key").get<std::string>()) : tiers.end();
        if (found != tiers.end() && found->is_number_integer() && found->get<int>() > 0) {
            next[pair.at("key").get<std::string>()] = 1;
        }
    }
    if (next != mTiers) {
        Persist(mObtained, next, mActiveGame, mClearedGames);
        mTiers = std::move(next);
        RebuildDeliveries();
    }
    return true;
}

void ComboSession::Persist(const std::set<std::string>& obtained, const nlohmann::json& tiers,
                           const std::string& active, const std::set<std::string>& cleared) const {
    WriteJson(mDirectory / "obtained.json", nlohmann::json{ { "obtained", obtained },
                                                            { "sharedTiers", tiers },
                                                            { "activeGame", active },
                                                            { "clearedGames", cleared },
                                                            { "bottles", mBottles },
                                                            { "live", mLive } });
}

bool ComboSession::SharesBottles() const {
    const uint64_t mask = ComboRando::SharedMaskFromKeys(mSeed.value("sharedItems", nlohmann::json::array()));
    return (mask & (uint64_t{ 1 } << ComboRando::SF_BOTTLES)) != 0;
}

bool ComboSession::ReportBottles(const std::string& game, const std::string& bottles) {
    if (!Enabled() || !IsGame(game) || !SharesBottles()) {
        return false;
    }
    if (mBottles.value("list", std::string{}) == bottles) {
        return true;
    }
    mBottles = { { "list", bottles }, { "source", game } };
    Persist(mObtained, mTiers, mActiveGame, mClearedGames);
    return true;
}

bool ComboSession::ReportLiveState(const std::string& game, const nlohmann::json& state) {
    if (!Enabled() || !IsGame(game) || !state.is_object()) {
        return false;
    }
    const uint64_t mask = ComboRando::SharedMaskFromKeys(mSeed.value("sharedItems", nlohmann::json::array()));
    const auto isShared = [mask](int family) { return (mask & (uint64_t{ 1 } << family)) != 0; };
    nlohmann::json shared = nlohmann::json::object();
    for (const auto& [field, family] :
         { std::pair{ "rupees", ComboRando::SF_LIVE_RUPEES }, std::pair{ "health", ComboRando::SF_LIVE_HEALTH },
           std::pair{ "magic", ComboRando::SF_LIVE_MAGIC }, std::pair{ "chateau", ComboRando::SF_LIVE_MAGIC },
           std::pair{ "equips", ComboRando::SF_LIVE_EQUIPS } }) {
        if (isShared(family) && state.contains(field)) {
            shared[field] = state.at(field);
        }
    }
    if (shared.empty() || mLive.value("state", nlohmann::json::object()) == shared) {
        return true;
    }
    mLive = { { "state", shared }, { "source", game } };
    return true;
}

std::string ComboSession::LiveStateFor(const std::string& game) const {
    if (!Enabled() || mLive.value("source", std::string{}) == game || !mLive.contains("state")) {
        return {};
    }
    return mLive.at("state").dump();
}

std::string ComboSession::BottlesFor(const std::string& game) const {
    if (!Enabled() || !SharesBottles() || mBottles.value("source", std::string{}) == game) {
        return {};
    }
    return mBottles.value("list", std::string{});
}

bool ComboSession::ReportGoalReached(const std::string& game) {
    if (!Enabled() || !IsGame(game)) {
        return true;
    }
    const nlohmann::json goal = mSeed.value("goal", nlohmann::json::object());
    auto next = mClearedGames;
    next.insert(game);
    if (next != mClearedGames) {
        Persist(mObtained, mTiers, mActiveGame, next);
        mClearedGames = std::move(next);
    }
    const bool isHunt = goal.value("type", std::string("bosses")) == "triforceHunt";
    const bool isEither = goal.value("mode", std::string("both")) == "either";
    return isHunt || isEither || mClearedGames.size() == 2;
}

bool ComboSession::ReportObtained(const std::string& game, const std::string& check) {
    if (!Enabled() || !IsGame(game) || !mSeed.at(game).at("placements").contains(check)) {
        return false;
    }
    const std::string key = game + ":" + check;
    if (mObtained.contains(key)) {
        return true;
    }
    const auto start = std::chrono::steady_clock::now();
    auto next = mObtained;
    next.insert(key);
    Persist(next, mTiers, mActiveGame, mClearedGames);
    mObtained = std::move(next);
    RebuildDeliveries();
    const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
    mSlowestReportMilliseconds = std::max(mSlowestReportMilliseconds, elapsed.count());
    return true;
}

double ComboSession::SlowestReportMilliseconds() const {
    return mSlowestReportMilliseconds;
}

namespace {
constexpr const char* kGivenPrefix = "give:";

std::optional<std::pair<std::string, std::string>> ParseGivenItem(const std::string& id) {
    if (id.rfind(kGivenPrefix, 0) != 0) {
        return std::nullopt;
    }
    const size_t gameEnd = id.find(':', std::strlen(kGivenPrefix));
    const size_t indexEnd = gameEnd == std::string::npos ? std::string::npos : id.find(':', gameEnd + 1);
    if (indexEnd == std::string::npos) {
        return std::nullopt;
    }
    const std::string game = id.substr(std::strlen(kGivenPrefix), gameEnd - std::strlen(kGivenPrefix));
    return std::make_pair(game, id.substr(indexEnd + 1));
}
struct ConsumablePair {
    const char* oot;
    const char* mm;
};

constexpr ConsumablePair kConsumables[] = {
    { "Green Rupee", "Green Rupee" },
    { "Blue Rupee", "Blue Rupee" },
    { "Red Rupee", "Red Rupee" },
    { "Purple Rupee", "Purple Rupee" },
    { "Huge Rupee", "Huge Rupee" },
    { "Recovery Heart", "Recovery Heart" },
    { "Red Potion Refill", "Red Potion Refill" },
    { "Green Potion Refill", "Green Potion Refill" },
    { "Blue Potion Refill", "Blue Potion Refill" },
    { "Arrows (10)", "10 Arrows" },
    { "Arrows (30)", "30 Arrows" },
    { "Bombs (5)", "5 Bombs" },
    { "Bombs (10)", "10 Bombs" },
    { "Bombchus (5)", "5 Bombchus" },
    { "Bombchus (10)", "10 Bombchus" },
    { "Deku Nuts (5)", "5 Deku Nuts" },
    { "Deku Nuts (10)", "10 Deku Nuts" },
    { "Deku Stick (1)", "Deku Stick" },
    { "Milk", "Milk Refill" },
};

bool IsReadAsOwned(int family) {
    const auto& definition = ComboRando::SharedFamilyByIndex(family);
    const bool hasOwnReader = family == ComboRando::SF_GRAB || family == ComboRando::SF_BLUE_FIRE;
    return definition.mmTierCap == 1 && definition.ootName[0] != '\0' && !hasOwnReader &&
           ComboRando::FindTierLadder(family) == nullptr;
}

int MmTierCap(int family) {
    const ComboRando::TierLadder* ladder = ComboRando::FindTierLadder(family);
    if (ladder == nullptr) {
        return ComboRando::SharedFamilyByIndex(family).mmTierCap;
    }
    size_t levels = 0;
    for (const ComboRando::CwLadderStep& step : ladder->mm) {
        levels += step.slots;
    }
    return static_cast<int>(levels);
}

std::optional<std::string> ConsumableTwin(const std::string& home, const std::string& item) {
    for (const ConsumablePair& pair : kConsumables) {
        if (item == (home == "oot" ? pair.oot : pair.mm)) {
            return std::string(home == "oot" ? pair.mm : pair.oot);
        }
    }
    return std::nullopt;
}

struct ChainSource {
    const char* game;
    const char* item;
    const char* pickup;
};

constexpr ChainSource kChainSources[] = {
    { "oot", "Kokiri Sword", "nei.progressive.kokiri_sword" },
    { "oot", "Master Sword", "nei.progressive.master_sword" },
    { "oot", "Megaton Hammer", "nei.progressive.hammer" },
    { "oot", "Giant's Knife", "nei.progressive.bgs" },
    { "oot", "Progressive Goron Sword", "nei.progressive.bgs" },
    { "oot", "Hookshot", "nei.progressive.hookshot" },
    { "oot", "Progressive Hookshot", "nei.progressive.hookshot" },
};
} // namespace

bool ComboSession::HasListedItems(const std::string& game) const {
    return mListedItems.contains(game);
}

void ComboSession::SetListedItems(const std::string& game, std::set<std::string> names) {
    if (!Enabled() || !IsGame(game)) {
        return;
    }
    mListedItems[game] = std::move(names);
    RebuildDeliveries();
}

std::string ComboSession::DeliveredName(const std::string& target, const std::string& item) const {
    const auto listed = mListedItems.find(target);
    if (listed == mListedItems.end()) {
        return item;
    }
    for (const ChainSource& source : kChainSources) {
        if (target == source.game && item == source.item && listed->second.contains(source.pickup)) {
            return source.pickup;
        }
    }
    return item;
}

std::optional<std::string> ComboSession::TwinName(const std::string& home, const std::string& item,
                                                  const std::string& target) const {
    if (target == home) {
        return item;
    }
    const nlohmann::json& goal = SeedObject(mSeed, "goal");
    const bool sharesPieces = goal.value("type", "bosses") == "triforceHunt" || goal.value("pieces", 0) > 0;
    if (sharesPieces && (item == "Triforce Piece" || item == "Piece of the Triforce")) {
        return std::string(target == "oot" ? "Triforce Piece" : "Piece of the Triforce");
    }
    const uint64_t mask = ComboRando::SharedMaskFromKeys(SeedArray(mSeed, "sharedItems"));
    const auto homeGame = home == "oot" ? ComboRando::GAME_OOT : ComboRando::GAME_MM;
    if (SharesBottles() && ComboRando::FindBottleTwin(homeGame, item) != nullptr) {
        return std::nullopt;
    }
    for (int family = 0; family < ComboRando::SF_COUNT; family++) {
        const auto& definition = ComboRando::SharedFamilyByIndex(family);
        if ((mask & (uint64_t{ 1 } << family)) == 0 ||
            item != (home == "oot" ? definition.ootName : definition.mmName)) {
            continue;
        }
        if (ComboRando::FindTierLadder(family) != nullptr) {
            return std::nullopt;
        }
        const std::string twin = target == "oot" ? definition.ootName : definition.mmName;
        return twin.empty() ? std::nullopt : std::optional<std::string>(twin);
    }
    for (const auto& pair : SeedArray(mSeed, "modFamilies")) {
        if (!pair.value("key", "").empty() && pair.value(home, "") == item) {
            return pair.value(target, item);
        }
    }
    return std::nullopt;
}

std::optional<std::string> ComboSession::MaskAlias(const std::string& game, const std::string& name) const {
    const std::string other = game == "oot" ? "mm" : "oot";
    const uint64_t mask = ComboRando::SharedMaskFromKeys(SeedArray(mSeed, "sharedItems"));
    const nlohmann::json& modPairs = SeedArray(mSeed, "modFamilies");
    for (int family = 0; family < ComboRando::SF_COUNT; family++) {
        const auto& definition = ComboRando::SharedFamilyByIndex(family);
        if ((mask & (uint64_t{ 1 } << family)) == 0 || !definition.isMask) {
            continue;
        }
        const std::string native = game == "oot" ? definition.ootName : definition.mmName;
        const std::string twin = game == "oot" ? definition.mmName : definition.ootName;
        for (const auto& pair : modPairs) {
            const std::string wearable = pair.value(game, "");
            if (pair.value(other, "") != twin || wearable.empty() || wearable == native) {
                continue;
            }
            if (name == native) {
                return wearable;
            }
            if (name == wearable) {
                return native;
            }
        }
    }
    return std::nullopt;
}

void ComboSession::QueueMaskAlias(nlohmann::json& queue, const std::string& game, const std::string& id,
                                  const std::string& item) const {
    if (const auto alias = MaskAlias(game, item)) {
        queue.push_back({ { "id", id + "+" + *alias }, { "item", *alias }, { "trap", false } });
    }
}

bool ComboSession::GiveItem(const std::string& game, const std::string& item) {
    return GiveItems({ { game, item } });
}

bool ComboSession::GiveItems(const std::vector<std::pair<std::string, std::string>>& items) {
    if (!Enabled() || std::any_of(items.begin(), items.end(),
                                  [](const auto& gift) { return !IsGame(gift.first) || gift.second.empty(); })) {
        return false;
    }
    auto given = std::count_if(mObtained.begin(), mObtained.end(),
                               [](const std::string& id) { return ParseGivenItem(id).has_value(); });
    auto next = mObtained;
    for (const auto& [game, item] : items) {
        next.insert(std::string(kGivenPrefix) + game + ":" + std::to_string(given++) + ":" + item);
    }
    Persist(next, mTiers, mActiveGame, mClearedGames);
    mObtained = std::move(next);
    RebuildDeliveries();
    return true;
}

nlohmann::json ComboSession::GivableItems() const {
    std::set<std::pair<std::string, std::string>> items;
    if (!Enabled()) {
        return nlohmann::json::array();
    }
    std::set<std::pair<std::string, std::string>> foreignChecks;
    for (const auto& foreign : mSeed.at("foreign")) {
        foreignChecks.emplace(foreign.at("checkGame").get<std::string>(), foreign.at("checkName").get<std::string>());
        items.emplace(foreign.at("itemGame").get<std::string>(), foreign.at("itemName").get<std::string>());
    }
    for (const char* game : { "oot", "mm" }) {
        for (const auto& [check, placed] : mSeed.at(game).at("placements").items()) {
            if (!foreignChecks.contains({ game, check })) {
                items.emplace(game, ComboRando::StripGameSuffix(placed.get<std::string>()));
            }
        }
        for (const auto& start : mSeed.at(game).at("settings").value("fleet.startingItems", nlohmann::json::array())) {
            items.emplace(game, start.get<std::string>());
        }
    }
    auto result = nlohmann::json::array();
    for (const auto& [game, item] : items) {
        result.push_back({ { "game", game }, { "item", item } });
    }
    return result;
}

void ComboSession::RebuildDeliveries() {
    const uint64_t mask = ComboRando::SharedMaskFromKeys(SeedArray(mSeed, "sharedItems"));
    std::map<std::string, nlohmann::json> deliveries = { { "oot", nlohmann::json::array() },
                                                         { "mm", nlohmann::json::array() } };
    for (const char* game : { "oot", "mm" }) {
        const auto starting = mSeed.at(game).at("settings").value("fleet.startingItems", nlohmann::json::array());
        for (size_t index = 0; index < starting.size(); index++) {
            deliveries[game].push_back({ { "id", std::string("start:") + game + ":" + std::to_string(index) },
                                         { "item", starting[index] },
                                         { "trap", false } });
        }
    }
    const ForeignIndex foreignByCheck = IndexForeignByCheck(mSeed);
    for (const char* source : { "oot", "mm" }) {
        const auto& placements = mSeed.at(source).at("placements");
        for (const auto& [check, placed] : placements.items()) {
            const std::string id = std::string(source) + ":" + check;
            if (!mObtained.contains(id)) {
                continue;
            }
            std::string home = source;
            std::string item = ComboRando::StripGameSuffix(placed.get<std::string>());
            const auto foreign = foreignByCheck.find({ source, check });
            const bool foreignItem = foreign != foreignByCheck.end();
            bool trap = false;
            if (foreignItem) {
                home = foreign->second->at("itemGame");
                item = foreign->second->at("itemName");
                trap = foreign->second->value("trap", false);
            }
            for (const char* target : { "oot", "mm" }) {
                if (!foreignItem && target == home) {
                    QueueMaskAlias(deliveries[target], target, id, item);
                    continue;
                }
                const bool isFinder = foreignItem && target == std::string(source);
                std::optional<std::string> grantName;
                if (!trap) {
                    grantName = TwinName(home, item, target);
                } else if (isFinder) {
                    grantName = item;
                }
                if (!trap && !grantName && isFinder) {
                    grantName = ConsumableTwin(home, item);
                }
                if (!grantName) {
                    continue;
                }
                deliveries[target].push_back({ { "id", id }, { "item", *grantName }, { "trap", trap } });
                if (!trap) {
                    QueueMaskAlias(deliveries[target], target, id, *grantName);
                }
            }
        }
    }
    for (const auto& id : mObtained) {
        const auto given = ParseGivenItem(id);
        if (!given) {
            continue;
        }
        for (const char* target : { "oot", "mm" }) {
            if (const auto grantName = TwinName(given->first, given->second, target)) {
                deliveries[target].push_back({ { "id", id }, { "item", *grantName }, { "trap", false } });
                QueueMaskAlias(deliveries[target], target, id, *grantName);
            }
        }
    }
    for (const char* game : { "oot", "mm" }) {
        for (auto& delivery : deliveries.at(game)) {
            delivery["item"] = DeliveredName(game, delivery.at("item").get<std::string>());
        }
        mDeliveries[game] = deliveries.at(game).dump();
        auto shared = nlohmann::json::array();
        for (int family = 0; family < ComboRando::SF_COUNT; family++) {
            const auto& definition = ComboRando::SharedFamilyByIndex(family);
            if ((mask & (uint64_t{ 1 } << family)) == 0) {
                continue;
            }
            const std::string tierKey = ComboRando::SharedTierKey(family);
            const int tier = mTiers.value(tierKey, 0);
            const std::string native = std::string(game) == "oot" ? definition.ootName : definition.mmName;
            shared.push_back({ { "key", tierKey },
                               { "item", DeliveredName(game, native) },
                               { "owned", IsReadAsOwned(family) },
                               { "tier", std::string(game) == "mm" ? std::min(tier, MmTierCap(family)) : tier } });
        }
        for (const auto& pair : SeedArray(mSeed, "modFamilies")) {
            if (!IsPairReadAsOwned(pair) || pair.value(game, "").empty()) {
                continue;
            }
            const std::string key = pair.at("key");
            shared.push_back({ { "key", key },
                               { "item", DeliveredName(game, pair.at(game).get<std::string>()) },
                               { "owned", true },
                               { "tier", mTiers.value(key, 0) } });
        }
        mSharedTiers[game] = shared.dump();
    }
}
