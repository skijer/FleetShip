#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <ship/config/ConsoleVariable.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

// Spawns every enemy key on a deterministic sample of spawn-table rows through the enemy_spawn_tester mod.
// A supervisor process relaunches fleet-test after a crash and resumes after the request that was in flight.
class EnemyWalk {
  public:
    struct Settings {
        std::filesystem::path spawns;
        std::filesystem::path candidates;
        std::string only;
        int frames = 600;
        int scenesPerEnemy = 5;
        size_t resume = 0;
        size_t shardIndex = 0;
        size_t shardCount = 1;
    };

    // "i/n" with 1 <= i <= n: this run takes every n-th request of the full list, starting at the i-th.
    static bool ParseShard(const std::string& text, Settings& settings) {
        const size_t slash = text.find('/');
        if (slash == std::string::npos) {
            return false;
        }
        const int index = std::atoi(text.substr(0, slash).c_str());
        const int count = std::atoi(text.substr(slash + 1).c_str());
        if (count < 1 || index < 1 || index > count) {
            return false;
        }
        settings.shardIndex = static_cast<size_t>(index - 1);
        settings.shardCount = static_cast<size_t>(count);
        return true;
    }

    struct Setting {
        std::string name;
        int32_t value;
    };

    struct Host {
        std::string game;
        std::filesystem::path gameDirectory;
        std::shared_ptr<Ship::ConsoleVariable> settings;
        std::vector<Setting> forcedSettings;
        std::function<void()> saveSettings;
        std::function<nlohmann::json(const std::string& game, const std::string& op, const nlohmann::json& input)>
            request;
        std::function<void(const std::string& from, const std::string& to)> travel;
        std::function<bool(const std::filesystem::path&)> capture;
    };

    EnemyWalk(Settings settings, const std::filesystem::path& logPath, Host host)
        : mSettings(std::move(settings)), mLogPath(logPath), mImages(ImagesOf(logPath)),
          mLog(logPath, std::ios::app), mHost(std::move(host)) {
        const std::filesystem::path spawns =
            mSettings.spawns.empty() ? FindSpawnTable(mHost.gameDirectory, mHost.game) : mSettings.spawns;
        LoadSpawnTable(spawns);
        LoadNativeCandidates(mSettings.candidates.empty() ? spawns.parent_path() /
                                                                ("EnemyCandidates-" + mHost.game + ".json")
                                                          : mSettings.candidates);
        std::filesystem::create_directories(mImages);
        ForceSettings();
        mHost.settings->SetString(kRequest, "");
        std::cout << "[fleet] enemy walk: " << mRows.size() << " spawn rows from " << spawns.string() << std::endl;
    }

    bool IsFinished() const {
        return mStep == Step::Finished;
    }

    int Failures() const {
        return mFailures;
    }

    void Advance(const std::string& active) {
        if (active != mHost.game && mStep != Step::Finished) {
            mHost.travel(active, mHost.game);
            return;
        }
        switch (mStep) {
            case Step::Catalogue:
                AwaitCatalogue();
                break;
            case Step::Request:
                RequestNext();
                break;
            case Step::Watch:
                WatchRequest();
                break;
            case Step::Finished:
                break;
            default:
                throw std::runtime_error("enemy walk reached an unknown step");
        }
    }

    static std::filesystem::path LogPathOf(const std::filesystem::path& installFolder, const std::string& game) {
        return installFolder / "logs" / ("fleet-enemy-walk-" + game + ".jsonl");
    }

    static bool IsSupervisorCall(int argc, char* argv[]) {
        const std::vector<std::string> args(argv + 1, argv + argc);
        return !ValueOf(args, "--enemy-walk").empty() && ValueOf(args, "--enemy-walk-resume").empty();
    }

    static int Supervise(int argc, char* argv[], const std::filesystem::path& installFolder) {
        const std::vector<std::string> args(argv + 1, argv + argc);
        const std::filesystem::path log = LogPathOf(installFolder, ValueOf(args, "--enemy-walk"));
        std::filesystem::create_directories(log.parent_path());
        std::ofstream(log, std::ios::trunc).close();
        std::filesystem::remove_all(ImagesOf(log));
        size_t next = 0;
        int bootFailures = 0;
        for (int attempt = 0;; attempt++) {
            const ChildExit exit = RunChild(argv[0], ChildArguments(args, next, attempt), log);
            const LogSummary summary = SummariseLog(log);
            if (summary.isFinished) {
                return Report(summary);
            }
            if (summary.inFlight.is_object()) {
                RecordCrash(log, summary.inFlight, exit);
                next = summary.inFlight.value("testing", size_t{ 0 }) + 1;
                bootFailures = 0;
                continue;
            }
            const bool hasProgressed = summary.nextIndex > next;
            next = summary.nextIndex;
            bootFailures = hasProgressed ? 0 : bootFailures + 1;
            if (bootFailures >= kMaxBootFailures) {
                std::cerr << "[fleet] enemy walk: fleet-test exited " << exit.code << " " << kMaxBootFailures
                          << " times without testing anything; giving up" << std::endl;
                return 2;
            }
        }
    }

  private:
    enum class Step { Catalogue, Request, Watch, Finished };

    struct Row {
        size_t index;
        int scene;
        int room;
        int actorId;
        int params;
        float x;
        float y;
        float z;
        int rotY;
        int roomFlags;
        int entrance;
        bool isLocation;
    };

    // One enemy the randomizer may place: a native catalog row or a mod enemy (actorId -1, resolved by key).
    struct Candidate {
        std::string key;
        int actorId = -1;
        int params = 0;
        int spawnHeight = 0;
        int rotX = 0;
        int rotZ = 0;
        uint32_t flags = 0;
    };

    struct Scene {
        std::string name;
        int entrance = -1;
        float extent = 0.0f;
    };

    struct Request {
        size_t row;
        std::string key;
        std::string category;
        std::string text;
    };

    struct Bounds {
        float minX = 0.0f;
        float maxX = 0.0f;
        float minZ = 0.0f;
        float maxZ = 0.0f;
        bool isEmpty = true;
    };

    struct ChildExit {
        int code = 0;
        bool isHung = false;
    };

    struct LogSummary {
        bool isFinished = false;
        nlohmann::json inFlight;
        size_t nextIndex = 0;
        int requests = 0;
        int failures = 0;
        int refusedSpots = 0;
        int crashes = 0;
    };

    static constexpr const char* kTesterSettings = "gMods.EnemySpawnTester.Auto";
    static constexpr const char* kRequest = "gMods.EnemySpawnTester.Auto.Request";
    static constexpr const char* kState = "gMods.EnemySpawnTester.Auto.State";
    static constexpr const char* kResult = "gMods.EnemySpawnTester.Auto.Result";
    static constexpr const char* kCancel = "gMods.EnemySpawnTester.Auto.Cancel";
    static constexpr const char* kEnemies = "gMods.EnemySpawnTester.Auto.Enemies";
    static constexpr int kCatalogueTimeoutFrames = 1800;
    static constexpr int kReadyTimeoutFrames = 600;
    static constexpr int kRequestTimeoutFrames = 6000;
    static constexpr int kCaptureDelayFrames = 10;
    static constexpr int kMaxBootFailures = 3;
    static constexpr int kHangSeconds = 180;
    static constexpr int kClearTrigger = 1;
    static constexpr int kRowSubmerged = 1 << 3;
    static constexpr int kRowSurface = 1 << 4;
    static constexpr int kRowMq = 1 << 5;
    static constexpr int kTimedRoom = 1 << 7;
    static constexpr uint32_t kNotInClearRooms = 1 << 1;
    static constexpr uint32_t kNotInTimedRooms = 1 << 2;
    static constexpr uint32_t kNeedsWater = 1 << 4;
    static constexpr uint32_t kUnderwater = 1 << 7;
    static constexpr uint32_t kSurfaceWalker = 1 << 16;
    static constexpr float kOpenExtent = 3000.0f;
    static constexpr float kSmallExtent = 600.0f;
    static constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    static constexpr uint64_t kFnvPrime = 1099511628211ull;

    static std::filesystem::path ImagesOf(const std::filesystem::path& log) {
        return log.parent_path() / log.stem();
    }

    static std::filesystem::path SettingsBackupOf(const std::filesystem::path& log) {
        return log.parent_path() / (log.stem().string() + ".settings.json");
    }

    static std::string ValueOf(const std::vector<std::string>& args, const std::string& flag) {
        for (size_t i = 0; i + 1 < args.size(); i += 2) {
            if (args[i] == flag) {
                return args[i + 1];
            }
        }
        return "";
    }

    // Works from the game install up: a junction to the build output resolves into the source checkout.
    static std::filesystem::path FindSpawnTable(const std::filesystem::path& gameDirectory, const std::string& game) {
        const std::filesystem::path relative = game == "oot"
                                                   ? "soh/soh/Enhancements/randomizer/StaticData/EnemySpawns.json"
                                                   : "mm/2s2h/Rando/StaticData/EnemySpawns.json";
        std::error_code error;
        std::filesystem::path folder = std::filesystem::canonical(gameDirectory, error);
        for (int depth = 0; depth < 5 && !error && !folder.empty(); depth++) {
            for (const auto& candidate : { folder / "EnemySpawns.json", folder / relative }) {
                if (std::filesystem::exists(candidate)) {
                    return candidate;
                }
            }
            folder = folder.parent_path();
        }
        throw std::runtime_error("no EnemySpawns.json found above " + gameDirectory.string() +
                                 "; pass --enemy-walk-spawns <path>");
    }

    struct UnsafeLinkSpot {
        const char* game;
        int scene;
        int room;
    };

    // The tester stands Link on the row: these warps land in another scene (Water Temple) or crash the camera
    // (Clock Town gallery's Octorok pen); a room of -1 covers the whole scene.
    static bool IsUnsafeLinkSpot(const std::string& game, int scene, int room) {
        static constexpr UnsafeLinkSpot kSpots[] = {
            { "oot", 5, 2 }, { "oot", 5, 4 }, { "oot", 5, 8 }, { "oot", 5, 14 }, { "oot", 5, 18 }, { "mm", 32, -1 },
        };
        return std::any_of(std::begin(kSpots), std::end(kSpots), [&](const UnsafeLinkSpot& spot) {
            return game == spot.game && scene == spot.scene && (spot.room < 0 || room == spot.room);
        });
    }

    // Bosses, spawners and room timers mark arenas or rules, not spots where an ordinary enemy stands.
    static bool IsLocationRow(const nlohmann::json& row) {
        const std::string name = row.value("actorName", std::string{});
        return row.value("category", std::string{}) != "ACTORCAT_BOSS" && !row.value("isSpawner", false) &&
               name != "ACTOR_OBJ_ROOMTIMER";
    }

    static void GrowBounds(Bounds& bounds, float x, float z) {
        bounds.minX = bounds.isEmpty ? x : std::min(bounds.minX, x);
        bounds.maxX = bounds.isEmpty ? x : std::max(bounds.maxX, x);
        bounds.minZ = bounds.isEmpty ? z : std::min(bounds.minZ, z);
        bounds.maxZ = bounds.isEmpty ? z : std::max(bounds.maxZ, z);
        bounds.isEmpty = false;
    }

    static float ExtentOf(const Bounds& bounds) {
        return std::max(bounds.maxX - bounds.minX, bounds.maxZ - bounds.minZ);
    }

    void LoadScenes(const nlohmann::json& table) {
        for (const auto& scene : table.value("scenes", nlohmann::json::array())) {
            Scene& entry = mScenes[scene.value("sceneId", -1)];
            entry.name = scene.value("name", std::string{});
            const auto& entrances = scene.value("entrances", nlohmann::json::array());
            entry.entrance = entrances.empty() ? -1 : entrances[0].value("entrance", -1);
        }
    }

    void LoadSpawnTable(const std::filesystem::path& path) {
        const nlohmann::json table = nlohmann::json::parse(std::ifstream(path), nullptr, false);
        if (!table.is_object() || !table.contains("rows")) {
            throw std::runtime_error("unreadable spawn table " + path.string());
        }
        LoadScenes(table);
        std::map<int, Bounds> sceneBounds;
        for (const auto& row : table["rows"]) {
            const int scene = row.value("sceneId", -1);
            const int room = row.value("roomIndex", 0);
            const int roomFlags = row.value("roomFlags", 0);
            // The walker boots vanilla dungeons: MQ rows keep their index but are never visited.
            const bool isMasterQuest = (roomFlags & kRowMq) != 0;
            const bool isLocation =
                !isMasterQuest && IsLocationRow(row) && !IsUnsafeLinkSpot(mHost.game, scene, room);
            mRows.push_back({ mRows.size(), scene, room, row.value("actorId", -1), row.value("params", 0),
                              row.value("posX", 0.0f), row.value("posY", 0.0f), row.value("posZ", 0.0f),
                              row.value("rotY", 0), roomFlags, row.value("entrance", mScenes[scene].entrance),
                              isLocation });
            if (isMasterQuest) {
                continue;
            }
            const Row& added = mRows.back();
            GrowBounds(sceneBounds[scene], added.x, added.z);
            GrowBounds(mRoomBounds[{ scene, added.room }], added.x, added.z);
            if (row.value("actorName", std::string{}) == "ACTOR_OBJ_ROOMTIMER" || (added.roomFlags & kTimedRoom)) {
                mTimedRooms.insert({ scene, added.room });
            }
        }
        for (const auto& [scene, bounds] : sceneBounds) {
            mScenes[scene].extent = ExtentOf(bounds);
        }
    }

    // Written by tools/enemy_spawns/extract_enemy_candidates.py from the host's enemy randomizer catalog.
    void LoadNativeCandidates(const std::filesystem::path& path) {
        const nlohmann::json table = nlohmann::json::parse(std::ifstream(path), nullptr, false);
        if (!table.is_object() || !table.contains("candidates")) {
            throw std::runtime_error("unreadable enemy candidates " + path.string() +
                                     "; run tools/enemy_spawns/extract_enemy_candidates.py or pass "
                                     "--enemy-walk-candidates <path>");
        }
        for (const auto& entry : table["candidates"]) {
            mNatives.push_back({ entry.value("key", std::string{}), entry.value("actorId", -1),
                                 entry.value("params", 0), entry.value("spawnHeight", 0), entry.value("rotX", 0),
                                 entry.value("rotZ", 0), entry.value("flags", 0u) });
        }
        for (const auto& pair : table.value("superseded", nlohmann::json::array())) {
            mSuperseded[pair.value("native", std::string{})] = pair.value("mod", std::string{});
        }
    }

    void Write(const nlohmann::json& line) {
        mLog << line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << std::endl;
    }

    // Values found before the first launch survive relaunches in a file, so a crash never makes a forced value stick.
    void ForceSettings() {
        const std::filesystem::path backup = SettingsBackupOf(mLogPath);
        nlohmann::json previous = nlohmann::json::parse(std::ifstream(backup), nullptr, false);
        if (!previous.is_object()) {
            previous = nlohmann::json::object();
            for (const Setting& setting : mHost.forcedSettings) {
                const char* name = setting.name.c_str();
                const bool isSet = mHost.settings->Get(name) != nullptr;
                previous[setting.name] =
                    isSet ? nlohmann::json(mHost.settings->GetInteger(name, 0)) : nlohmann::json(nullptr);
            }
            std::ofstream(backup) << previous.dump(1);
        }
        for (const Setting& setting : mHost.forcedSettings) {
            mHost.settings->SetInteger(setting.name.c_str(), setting.value);
        }
    }

    void RestoreSettings() {
        const std::filesystem::path backup = SettingsBackupOf(mLogPath);
        const nlohmann::json previous = nlohmann::json::parse(std::ifstream(backup), nullptr, false);
        for (const Setting& setting : mHost.forcedSettings) {
            const nlohmann::json value =
                previous.is_object() ? previous.value(setting.name, nlohmann::json()) : nlohmann::json(nullptr);
            if (value.is_number_integer()) {
                mHost.settings->SetInteger(setting.name.c_str(), value.get<int32_t>());
            } else {
                mHost.settings->ClearVariable(setting.name.c_str());
            }
        }
        mHost.settings->ClearBlock(kTesterSettings);
        mHost.saveSettings();
        std::filesystem::remove(backup);
    }

    bool IsGameReady() {
        const nlohmann::json status = mHost.request(mHost.game, "TestStatus", nlohmann::json::object());
        return status.value("playing", false) && !status.value("transitioning", true);
    }

    // The tester mod publishes every registered mod enemy as "key,flags" entries separated by '|'.
    std::vector<Candidate> ListModCandidates() const {
        std::vector<Candidate> candidates;
        std::istringstream list(mHost.settings->GetString(kEnemies, ""));
        for (std::string entry; std::getline(list, entry, '|');) {
            if (entry.empty()) {
                continue;
            }
            const size_t comma = entry.find(',');
            Candidate candidate;
            candidate.key = entry.substr(0, comma);
            candidate.flags = comma == std::string::npos ? 0 : std::stoul(entry.substr(comma + 1));
            candidates.push_back(candidate);
        }
        return candidates;
    }

    void AwaitCatalogue() {
        const bool isPublished = mHost.settings->Get(kEnemies) != nullptr;
        if (!isPublished || !IsGameReady()) {
            if (++mWaitedFrames > kCatalogueTimeoutFrames) {
                throw std::runtime_error("enemy_spawn_tester never published its enemy list: is the mod installed?");
            }
            return;
        }
        mRequests = BuildRequests(ListModCandidates());
        mNext = mSettings.resume;
        std::cout << "[fleet] enemy walk: " << mRequests.size() << " requests, starting at " << mNext << " into "
                  << mLogPath.string() << std::endl;
        Await(Step::Request);
    }

    // A fragment naming a scene ("scene:<id>" or part of its name) narrows the rows; any other narrows the keys.
    bool IsSceneFragment(const std::string& fragment) const {
        if (fragment.rfind("scene:", 0) == 0) {
            return true;
        }
        return std::any_of(mScenes.begin(), mScenes.end(), [&](const auto& scene) {
            return !scene.second.name.empty() && scene.second.name.find(fragment) != std::string::npos;
        });
    }

    bool IsSceneSelected(int scene, const std::vector<std::string>& fragments) const {
        if (fragments.empty()) {
            return true;
        }
        const auto found = mScenes.find(scene);
        const std::string name = found != mScenes.end() ? found->second.name : "";
        const std::string token = "scene:" + std::to_string(scene);
        return std::any_of(fragments.begin(), fragments.end(), [&](const std::string& fragment) {
            return fragment == token || (!name.empty() && name.find(fragment) != std::string::npos);
        });
    }

    static bool IsKeySelected(const std::string& key, const std::vector<std::string>& fragments) {
        return fragments.empty() || std::any_of(fragments.begin(), fragments.end(), [&](const std::string& fragment) {
                   return key.find(fragment) != std::string::npos;
               });
    }

    static uint64_t HashOf(const std::string& text) {
        uint64_t hash = kFnvOffset;
        for (const unsigned char c : text) {
            hash = (hash ^ c) * kFnvPrime;
        }
        return hash;
    }

    std::optional<size_t> PickRow(const std::vector<size_t>& candidates, uint64_t seed,
                                  const std::set<int>& usedScenes) const {
        for (size_t step = 0; step < candidates.size(); step++) {
            const size_t row = candidates[(seed + step) % candidates.size()];
            if (!usedScenes.contains(mRows[row].scene)) {
                return row;
            }
        }
        return std::nullopt;
    }

    static bool IsWaterRow(const Row& row) {
        return (row.roomFlags & (kRowSubmerged | kRowSurface)) != 0;
    }

    // The randomizer's water classes (contract section 6): submerged, surface and dry rows.
    static bool FitsRowWater(const Row& row, const Candidate& candidate) {
        const bool needsWater = (candidate.flags & kNeedsWater) != 0;
        const bool underwater = (candidate.flags & kUnderwater) != 0;
        if (row.roomFlags & kRowSubmerged) {
            return underwater;
        }
        if (row.roomFlags & kRowSurface) {
            return (needsWater && !underwater) || (candidate.flags & kSurfaceWalker) != 0;
        }
        return !needsWater;
    }

    // The same placement rules the randomizer applies: water, clear-room and timed-room flags of the candidate.
    bool IsAllowedAt(const Row& row, const Candidate& candidate) const {
        const bool isClear = (row.roomFlags & kClearTrigger) != 0;
        const bool isTimed = mTimedRooms.contains({ row.scene, row.room });
        if (!row.isLocation || !FitsRowWater(row, candidate)) {
            return false;
        }
        return !((candidate.flags & kNotInClearRooms) && isClear) && !((candidate.flags & kNotInTimedRooms) && isTimed);
    }

    bool IsInCategory(const Row& row, const std::string& category, const Candidate& candidate) const {
        const bool isWater = IsWaterRow(row);
        if (category == "home") {
            return candidate.actorId >= 0 && row.actorId == candidate.actorId;
        }
        if (category == "clear") {
            return (row.roomFlags & kClearTrigger) != 0;
        }
        if (category == "water") {
            return isWater;
        }
        if (category == "open") {
            return !isWater && mScenes.at(row.scene).extent >= kOpenExtent;
        }
        if (category == "small") {
            return !isWater && ExtentOf(mRoomBounds.at({ row.scene, row.room })) <= kSmallExtent;
        }
        return category == "extra";
    }

    // One row per category in a scene not yet used by this key, then extra scenes until the per-enemy budget is met.
    std::vector<std::pair<size_t, std::string>> SampleRows(const Candidate& candidate,
                                                           const std::vector<size_t>& allowedRows) const {
        static const std::vector<std::string> kCategories = { "home", "clear", "water", "open", "small" };
        std::vector<std::pair<size_t, std::string>> picks;
        std::set<int> usedScenes;
        const auto pickFrom = [&](const std::string& category, const std::string& salt) {
            std::vector<size_t> spots;
            std::copy_if(allowedRows.begin(), allowedRows.end(), std::back_inserter(spots), [&](size_t row) {
                return IsAllowedAt(mRows[row], candidate) && IsInCategory(mRows[row], category, candidate);
            });
            const std::optional<size_t> row = PickRow(spots, HashOf(candidate.key + "/" + salt), usedScenes);
            if (row) {
                picks.emplace_back(*row, category);
                usedScenes.insert(mRows[*row].scene);
            }
            return row.has_value();
        };
        for (const std::string& category : kCategories) {
            if (static_cast<int>(picks.size()) < mSettings.scenesPerEnemy) {
                pickFrom(category, category);
            }
        }
        for (int extra = 0; static_cast<int>(picks.size()) < mSettings.scenesPerEnemy; extra++) {
            if (!pickFrom("extra", "extra" + std::to_string(extra))) {
                break;
            }
        }
        return picks;
    }

    std::string FormatRequest(const Row& row, const Candidate& candidate) const {
        std::ostringstream text;
        text << row.entrance << '|' << row.index << '|' << candidate.key << '|' << mSettings.frames << '|' << row.scene
             << '|' << row.room << '|' << row.x << '|' << row.y << '|' << row.z << '|' << row.rotY << '|'
             << candidate.actorId << '|' << candidate.params << '|' << candidate.spawnHeight << '|' << candidate.rotX
             << '|' << candidate.rotZ << '|' << candidate.flags;
        return text.str();
    }

    // Natives a registered mod enemy replaces are left out, as the randomizer does.
    std::vector<Candidate> ListCandidates(const std::vector<Candidate>& modCandidates) const {
        std::set<std::string> modKeys;
        for (const Candidate& candidate : modCandidates) {
            modKeys.insert(candidate.key);
        }
        std::vector<Candidate> candidates;
        for (const Candidate& native : mNatives) {
            const auto superseded = mSuperseded.find(native.key);
            if (superseded == mSuperseded.end() || !modKeys.contains(superseded->second)) {
                candidates.push_back(native);
            }
        }
        candidates.insert(candidates.end(), modCandidates.begin(), modCandidates.end());
        return candidates;
    }

    std::vector<Request> BuildRequests(const std::vector<Candidate>& modCandidates) const {
        std::vector<std::string> keyFragments;
        std::vector<std::string> sceneFragments;
        std::istringstream only(mSettings.only);
        for (std::string fragment; std::getline(only, fragment, '|');) {
            if (!fragment.empty()) {
                (IsSceneFragment(fragment) ? sceneFragments : keyFragments).push_back(fragment);
            }
        }
        std::vector<size_t> allowedRows;
        for (const Row& row : mRows) {
            if (IsSceneSelected(row.scene, sceneFragments)) {
                allowedRows.push_back(row.index);
            }
        }
        std::vector<Request> requests;
        size_t position = 0;
        for (const Candidate& candidate : ListCandidates(modCandidates)) {
            if (!IsKeySelected(candidate.key, keyFragments)) {
                continue;
            }
            for (const auto& [row, category] : SampleRows(candidate, allowedRows)) {
                if (position++ % mSettings.shardCount == mSettings.shardIndex) {
                    requests.push_back({ row, candidate.key, category, FormatRequest(mRows[row], candidate) });
                }
            }
        }
        return requests;
    }

    void Await(Step step) {
        mStep = step;
        mWaitedFrames = 0;
    }

    nlohmann::json DescribeRequest() const {
        const Request& request = mRequests[mNext];
        const Row& row = mRows[request.row];
        const auto scene = mScenes.find(row.scene);
        return { { "testing", mNext },
                 { "key", request.key },
                 { "category", request.category },
                 { "scene", row.scene },
                 { "sceneName", scene != mScenes.end() ? scene->second.name : "" },
                 { "row", row.index },
                 { "request", request.text } };
    }

    void RequestNext() {
        if (mNext >= mRequests.size()) {
            Finish();
            return;
        }
        if (!IsGameReady() && ++mWaitedFrames <= kReadyTimeoutFrames) {
            return;
        }
        Write(DescribeRequest());
        mRecord = DescribeRequest();
        mRecord.erase("testing");
        mRecord["index"] = mNext;
        mHost.settings->SetString(kState, "");
        mHost.settings->SetString(kResult, "");
        mHost.settings->SetString(kRequest, mRequests[mNext].text.c_str());
        mHasCapture = false;
        mActiveFrames = 0;
        Await(Step::Watch);
    }

    static bool IsFinal(const std::string& state) {
        static const std::set<std::string> kFinalStates = { "ok",          "despawned_early", "out_of_bounds",
                                                            "stuck_death", "unspawnable",     "unknown",
                                                            "cancelled" };
        return kFinalStates.contains(state);
    }

    // An enemy that refuses a spot (e.g. an Octorok outside water) reports a placement rule, not a failure.
    static bool IsRefusedSpot(const std::string& state, const std::string& reason) {
        return state == "unspawnable" && reason == "refused_spot";
    }

    static bool IsPassing(const std::string& state, const std::string& reason) {
        return state == "ok" || IsRefusedSpot(state, reason);
    }

    static std::string ReasonOf(const nlohmann::json& record) {
        const nlohmann::json result = record.value("result", nlohmann::json::object());
        return result.is_object() ? result.value("reason", std::string{}) : std::string{};
    }

    static bool IsEnemyOut(const std::string& state) {
        return state == "watching" || state == "killing" || state == "dying";
    }

    std::string NameImage() const {
        std::ostringstream name;
        name << std::setw(4) << std::setfill('0') << mNext + 1 << ".ppm";
        return name.str();
    }

    void CaptureOnce(const std::string& state) {
        const bool isDue = IsFinal(state) || (IsEnemyOut(state) && ++mActiveFrames >= kCaptureDelayFrames);
        if (mHasCapture || !isDue) {
            return;
        }
        const std::string image = NameImage();
        mRecord["image"] = mHost.capture(mImages / image) ? image : "";
        mHasCapture = true;
    }

    void WatchRequest() {
        const bool isTaken = std::string(mHost.settings->GetString(kRequest, "")).empty();
        const std::string state = isTaken ? mHost.settings->GetString(kState, "") : "";
        CaptureOnce(state);
        if (IsFinal(state)) {
            RecordResult(state);
            return;
        }
        if (++mWaitedFrames > kRequestTimeoutFrames) {
            mHost.settings->SetString(kRequest, "");
            mHost.settings->SetInteger(kCancel, 1);
            RecordResult(isTaken ? "timeout after " + state : "timeout: request never taken");
        }
    }

    void RecordResult(const std::string& state) {
        const nlohmann::json result = nlohmann::json::parse(mHost.settings->GetString(kResult, ""), nullptr, false);
        mRecord["state"] = state;
        if (result.is_object()) {
            mRecord["result"] = result;
        }
        Write(mRecord);
        const std::string reason = ReasonOf(mRecord);
        mFailures += IsPassing(state, reason) ? 0 : 1;
        std::cout << "[fleet] enemy walk " << state << " " << mNext + 1 << "/" << mRequests.size() << ": "
                  << mRecord.value("key", "") << " in scene " << mRecord.value("scene", -1)
                  << (reason.empty() ? "" : " (" + reason + ")") << std::endl;
        mNext++;
        Await(Step::Request);
    }

    void Finish() {
        RestoreSettings();
        Write({ { "finished", true }, { "requests", mRequests.size() } });
        mStep = Step::Finished;
    }

    static std::vector<std::string> ChildArguments(std::vector<std::string> args, size_t next, int attempt) {
        args.push_back("--enemy-walk-resume");
        args.push_back(std::to_string(next));
        if (attempt == 0) {
            return args;
        }
        const std::string retry = "-retry" + std::to_string(attempt);
        for (size_t i = 0; i + 1 < args.size(); i += 2) {
            if (args[i] == "--session") {
                args[i + 1] += retry;
                return args;
            }
        }
        const std::string seed = ValueOf(args, "--seed");
        if (!seed.empty()) {
            const std::filesystem::path seedPath(seed);
            args.push_back("--session");
            args.push_back((seedPath.parent_path() / (seedPath.stem().string() + ".enemy-walk" + retry)).string());
        }
        return args;
    }

    static std::filesystem::file_time_type LastChangeOf(const std::filesystem::path& log) {
        std::error_code error;
        const auto time = std::filesystem::last_write_time(log, error);
        return error ? std::filesystem::file_time_type{} : time;
    }

#ifdef _WIN32
    static std::wstring ToWide(const std::string& text) {
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        std::wstring wide(static_cast<size_t>(std::max(length, 1)) - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
        return wide;
    }

    // CommandLineToArgvW rules: backslashes only escape when they precede a quote.
    static std::wstring QuoteArgument(const std::wstring& argument) {
        std::wstring quoted = L"\"";
        size_t backslashes = 0;
        for (const wchar_t c : argument) {
            if (c == L'\\') {
                backslashes++;
                continue;
            }
            quoted.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(c);
        }
        quoted.append(backslashes * 2, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }

    static ChildExit RunChild(const char*, const std::vector<std::string>& args, const std::filesystem::path& log) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring line = QuoteArgument(exe);
        for (const std::string& arg : args) {
            line += L" " + QuoteArgument(ToWide(arg));
        }
        STARTUPINFOW startup = { sizeof(startup) };
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION process = {};
        if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process)) {
            throw std::runtime_error("could not relaunch fleet-test for the enemy walk");
        }
        ChildExit exit;
        auto lastChange = LastChangeOf(log);
        auto lastProgress = std::chrono::steady_clock::now();
        while (WaitForSingleObject(process.hProcess, 2000) == WAIT_TIMEOUT) {
            if (LastChangeOf(log) != lastChange) {
                lastChange = LastChangeOf(log);
                lastProgress = std::chrono::steady_clock::now();
            } else if (std::chrono::steady_clock::now() - lastProgress > std::chrono::seconds(kHangSeconds)) {
                TerminateProcess(process.hProcess, 3);
                exit.isHung = true;
            }
        }
        DWORD code = 0;
        GetExitCodeProcess(process.hProcess, &code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        exit.code = static_cast<int>(code);
        return exit;
    }
#else
    static ChildExit RunChild(const char* self, const std::vector<std::string>& args,
                              const std::filesystem::path& log) {
        std::error_code error;
        const std::filesystem::path linked = std::filesystem::read_symlink("/proc/self/exe", error);
        const std::string exe = error ? std::string(self) : linked.string();
        std::vector<std::string> owned = { exe };
        owned.insert(owned.end(), args.begin(), args.end());
        std::vector<char*> argv;
        for (std::string& arg : owned) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        const pid_t child = fork();
        if (child == 0) {
            execv(exe.c_str(), argv.data());
            _exit(127);
        }
        ChildExit exit;
        auto lastChange = LastChangeOf(log);
        auto lastProgress = std::chrono::steady_clock::now();
        int status = 0;
        while (waitpid(child, &status, WNOHANG) == 0) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (LastChangeOf(log) != lastChange) {
                lastChange = LastChangeOf(log);
                lastProgress = std::chrono::steady_clock::now();
            } else if (std::chrono::steady_clock::now() - lastProgress > std::chrono::seconds(kHangSeconds)) {
                kill(child, SIGKILL);
                exit.isHung = true;
            }
        }
        exit.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        return exit;
    }
#endif

    static LogSummary SummariseLog(const std::filesystem::path& log) {
        LogSummary summary;
        std::ifstream lines(log);
        for (std::string text; std::getline(lines, text);) {
            const nlohmann::json line = nlohmann::json::parse(text, nullptr, false);
            if (!line.is_object()) {
                continue;
            }
            if (line.contains("finished")) {
                summary.isFinished = true;
            } else if (line.contains("testing")) {
                summary.inFlight = line;
            } else if (line.contains("state")) {
                summary.inFlight = nullptr;
                summary.nextIndex = line.value("index", size_t{ 0 }) + 1;
                const std::string state = line.value("state", "");
                const std::string reason = ReasonOf(line);
                summary.requests++;
                summary.failures += IsPassing(state, reason) ? 0 : 1;
                summary.refusedSpots += IsRefusedSpot(state, reason) ? 1 : 0;
                summary.crashes += state == "crash" ? 1 : 0;
            }
        }
        return summary;
    }

    static void RecordCrash(const std::filesystem::path& log, nlohmann::json request, const ChildExit& exit) {
        request["index"] = request["testing"];
        request.erase("testing");
        request["state"] = "crash";
        request["exitCode"] = exit.code;
        request["reason"] = exit.isHung ? "hung: no progress for " + std::to_string(kHangSeconds) + " s"
                                        : "process exited";
        std::ofstream(log, std::ios::app) << request.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)
                                          << std::endl;
        std::cout << "[fleet] enemy walk crash on " << request.value("key", "") << " in scene "
                  << request.value("scene", -1) << " (exit " << exit.code << "); relaunching" << std::endl;
    }

    static int Report(const LogSummary& summary) {
        std::cout << "[fleet] enemy walk finished: " << summary.requests << " requests, " << summary.failures
                  << " failures, " << summary.refusedSpots << " refused spots, " << summary.crashes << " crashes"
                  << std::endl;
        return summary.failures == 0 ? 0 : 1;
    }

    Settings mSettings;
    std::filesystem::path mLogPath;
    std::filesystem::path mImages;
    std::ofstream mLog;
    Host mHost;
    std::vector<Row> mRows;
    std::map<int, Scene> mScenes;
    std::map<std::pair<int, int>, Bounds> mRoomBounds;
    std::set<std::pair<int, int>> mTimedRooms;
    std::vector<Candidate> mNatives;
    std::map<std::string, std::string> mSuperseded;
    std::vector<Request> mRequests;
    Step mStep = Step::Catalogue;
    size_t mNext = 0;
    int mWaitedFrames = 0;
    int mActiveFrames = 0;
    bool mHasCapture = false;
    nlohmann::json mRecord;
    int mFailures = 0;
};
