#include "ComboSession.h"
#include "CrossDungeonRuntime.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void VerifyPortalRoutes() {
    using namespace ComboRando::CrossDungeon;
    const std::vector<Dungeon> catalogue = { { "oot:test", { 0, 10 }, { 0, 20 }, { 0, 30 } },
                                             { "mm:test", { 1, 40 }, { 1, 50 }, { 1, 60 } } };
    const auto seed = Encode(catalogue, { 1, 0 });
    RuntimeState oot(0);
    RuntimeState mm(1);
    oot.Install(seed);
    mm.Install(seed);
    const auto outbound = oot.Lookup(Phase::AfterNative, 10);
    Require(outbound && outbound->to == Endpoint{ 1, 40 } && outbound->checkpoint == Endpoint{ 0, 30 },
            "portal did not select its destination and safe source checkpoint");
    Require(!oot.Lookup(Phase::BeforeNative, 10), "portal ran in the wrong native phase");
    Require(mm.QueueArrival(40) && mm.RecoveryForArrival(40) == 40, "peer arrival was not recoverable");
    Require(!mm.QueueArrival(10), "peer accepted an arrival belonging to another game");
    Require(mm.ConsumeArrival() == 40 && mm.ConsumeArrival() == -1, "arrival was lost or replayed");
    const auto returning = mm.Lookup(Phase::BeforeNative, 50);
    Require(returning && returning->to == Endpoint{ 0, 30 } && returning->checkpoint == Endpoint{ 1, 40 },
            "exit did not return to the shuffled source");
    Require(oot.QueueArrival(30), "source rejected the paired return destination");
    oot.Install(seed);
    Require(oot.ConsumeArrival() == 30, "reinstalling the same routes discarded a pending arrival");
}

size_t CountDeliveries(const ComboSession& session, const char* game, const std::string& item) {
    const auto deliveries = nlohmann::json::parse(session.Deliveries(game));
    return std::count_if(deliveries.begin(), deliveries.end(),
                         [&](const nlohmann::json& delivery) { return delivery.at("item") == item; });
}

// OoT holds the Bunny Hood twice, as its trade mask and as a mod's wearable: one shared copy gives both.
void VerifyMaskAliases(const std::filesystem::path& folder) {
    nlohmann::json seed = {
        { "fileType", "ComboShipRandomizer" },
        { "startingGame", "OOT" },
        { "sharedItems", { "bunnyHood" } },
        { "modFamilies",
          nlohmann::json::array(
              { { { "key", "nei.mask_bunny" }, { "oot", "nei.mask_bunny" }, { "mm", "Bunny Hood" } } }) },
        { "oot",
          { { "settings", nlohmann::json::object() },
            { "placements", { { "Own Chest", "Bunny Hood" }, { "Wearable Chest", "nei.mask_bunny" } } } } },
        { "mm", { { "settings", nlohmann::json::object() }, { "placements", { { "Mask Chest", "Bunny Hood" } } } } },
        { "foreign", nlohmann::json::array() }
    };
    const auto seedPath = folder / "mask-seed.json";
    std::ofstream(seedPath) << seed;
    ComboSession fromMm;
    fromMm.Open(seedPath, folder / "mask-mm", 0);
    Require(fromMm.ReportObtained("mm", "Mask Chest"), "mask pickup rejected");
    Require(CountDeliveries(fromMm, "oot", "Bunny Hood") == 1, "MM's mask did not reach OoT");
    Require(CountDeliveries(fromMm, "oot", "nei.mask_bunny") == 1, "MM's mask did not bring OoT's wearable");
    Require(nlohmann::json::parse(fromMm.Deliveries("mm")).empty(), "MM's own mask came back to it");
    ComboSession fromOot;
    fromOot.Open(seedPath, folder / "mask-oot", 0);
    Require(fromOot.ReportObtained("oot", "Own Chest"), "trade mask pickup rejected");
    Require(CountDeliveries(fromOot, "oot", "nei.mask_bunny") == 1, "OoT's trade mask did not bring its wearable");
    Require(CountDeliveries(fromOot, "oot", "Bunny Hood") == 0, "OoT's own trade mask was delivered again");
    Require(CountDeliveries(fromOot, "mm", "Bunny Hood") == 1, "OoT's trade mask did not reach MM");
    ComboSession fromWearable;
    fromWearable.Open(seedPath, folder / "mask-wearable", 0);
    Require(fromWearable.ReportObtained("oot", "Wearable Chest"), "wearable pickup rejected");
    Require(CountDeliveries(fromWearable, "oot", "Bunny Hood") == 1, "OoT's wearable did not bring its trade mask");
    Require(CountDeliveries(fromWearable, "mm", "Bunny Hood") == 1, "OoT's wearable did not reach MM");
}

void VerifyGoalReached(const std::filesystem::path& folder) {
    nlohmann::json seed = {
        { "fileType", "ComboShipRandomizer" },
        { "startingGame", "OOT" },
        { "goal", { { "type", "bosses" }, { "mode", "both" } } },
        { "oot", { { "settings", nlohmann::json::object() }, { "placements", { { "Chest", "Hookshot" } } } } },
        { "mm", { { "settings", nlohmann::json::object() }, { "placements", { { "Chest", "Hookshot" } } } } },
        { "foreign", nlohmann::json::array() }
    };
    const auto bothPath = folder / "goal-both.json";
    std::ofstream(bothPath) << seed;
    ComboSession both;
    both.Open(bothPath, folder / "goal-both", 0);
    Require(!both.ReportGoalReached("oot"), "both: one cleared game rolled the credits");
    ComboSession reloaded;
    reloaded.Open(bothPath, folder / "goal-both", 0);
    Require(!reloaded.ReportGoalReached("oot"), "both: a repeated report counted twice");
    Require(reloaded.ReportGoalReached("mm"), "both: the second game did not roll the credits");
    seed["goal"]["mode"] = "either";
    const auto eitherPath = folder / "goal-either.json";
    std::ofstream(eitherPath) << seed;
    ComboSession either;
    either.Open(eitherPath, folder / "goal-either", 0);
    Require(either.ReportGoalReached("mm"), "either: one cleared game did not roll the credits");
}

} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto folder = std::filesystem::temp_directory_path() / ("fleet-session-test-" + std::to_string(unique));
    std::filesystem::create_directories(folder);
    try {
        VerifyPortalRoutes();
        VerifyGoalReached(folder);
        nlohmann::json seed = {
            { "fileType", "ComboShipRandomizer" },
            { "startingGame", "MM" },
            { "sharedItems", { "bows", "fireArrows" } },
            { "oot",
              { { "settings", nlohmann::json::object() },
                { "placements", { { "Chest A", "Hookshot" }, { "Chest B", "Fire Arrows" } } } } },
            { "mm", { { "settings", nlohmann::json::object() }, { "placements", { { "Chest C", "Fire Arrows" } } } } },
            { "foreign", nlohmann::json::array({ { { "checkGame", "oot" },
                                                   { "checkName", "Chest A" },
                                                   { "itemGame", "mm" },
                                                   { "itemName", "Hookshot" } },
                                                 { { "checkGame", "mm" },
                                                   { "checkName", "Chest C" },
                                                   { "itemGame", "oot" },
                                                   { "itemName", "Fire Arrows" },
                                                   { "shared", true } } }) }
        };
        seed["modFamilies"] =
            nlohmann::json::array({ { { "key", "nei.test_rod" }, { "oot", "oot rod" }, { "mm", "mm rod" } } });
        seed["oot"]["placements"]["Mod A"] = "oot rod";
        seed["mm"]["placements"]["Mod B"] = "mm rod";
        const auto seedPath = folder / "seed-input.json";
        std::ofstream(seedPath) << seed;
        ComboSession session;
        session.Open(seedPath, folder / "session", 0);
        Require(std::filesystem::is_directory(folder / "session" / "slot0" / "oot" / "Save"),
                "new native OoT save has no writable save directory");
        Require(session.ResumeGame() == "mm", "starting game was lost");
        Require(!session.ReportObtained("oot", "unknown"), "unknown check was accepted");
        Require(session.ReportObtained("oot", "Chest A"), "foreign pickup was rejected");
        Require(session.ReportObtained("oot", "Chest A"), "repeat report should succeed");
        Require(nlohmann::json::parse(session.Deliveries("mm")).size() == 1, "foreign grant duplicated");
        Require(nlohmann::json::parse(session.Deliveries("oot")).empty(), "foreign grant went to finder");
        Require(session.ReportObtained("oot", "Chest B"), "native shared pickup was rejected");
        Require(nlohmann::json::parse(session.Deliveries("mm")).size() == 2, "native shared pickup was not mirrored");
        Require(session.ReportObtained("mm", "Chest C"), "foreign shared pickup was rejected");
        Require(session.ReportSharedTiers("oot", { { "bows", 2 } }), "shared tiers were rejected");
        Require(session.ReportSharedTiers("mm", { { "bows", 1 } }), "peer tier was rejected");
        Require(nlohmann::json::parse(session.SharedTiers("mm")).at(0).at("tier") == 2, "shared tiers decreased");
        Require(nlohmann::json::parse(session.Deliveries("oot")).size() == 1, "foreign shared did not reach OoT");
        Require(nlohmann::json::parse(session.Deliveries("mm")).size() == 3, "foreign shared did not reach finder");
        Require(session.ReportObtained("oot", "Mod A"), "mod pickup rejected");
        Require(session.ReportObtained("mm", "Mod B"), "peer mod pickup rejected");
        Require(session.Deliveries("mm").find("mm rod") != std::string::npos, "OoT mod pickup did not grant MM alias");
        Require(session.Deliveries("oot").find("oot rod") != std::string::npos,
                "MM mod pickup did not grant OoT alias");
        const auto beforeRepeat = session.Deliveries("mm");
        session.ReportObtained("oot", "Mod A");
        Require(session.Deliveries("mm") == beforeRepeat, "repeated mod report duplicated a grant");
        ComboSession reloaded;
        session.SetActiveGame("oot");
        session.RequestCommit();
        session.CommitIfRequested();
        reloaded.Open(seedPath, folder / "session", 0);
        Require(reloaded.ResumeGame() == "oot", "active game did not survive restart");
        Require(reloaded.Deliveries("mm") == session.Deliveries("mm"), "pending grants did not survive restart");
        Require(reloaded.SharedTiers("oot") == session.SharedTiers("oot"), "shared tiers did not survive restart");
        std::ofstream(folder / "session" / "slot0" / "oot" / "Save" / "file1.sav") << "unsaved";
        reloaded.SetActiveGame("mm");
        ComboSession reset;
        reset.Open(seedPath, folder / "session", 0);
        Require(reset.ResumeGame() == "oot", "a reset kept the game the player had not saved in");
        Require(!std::filesystem::exists(folder / "session" / "slot0" / "oot" / "Save" / "file1.sav"),
                "a reset kept an unsaved game file");
        ComboSession otherSlot;
        otherSlot.Open(seedPath, folder / "session", 1);
        Require(nlohmann::json::parse(otherSlot.Deliveries("mm")).empty(), "slots share pickup history");
        seed["startingGame"] = "OOT";
        std::ofstream(seedPath) << seed;
        bool rejected = false;
        try {
            reloaded.Open(seedPath, folder / "session", 0);
        } catch (const std::invalid_argument&) { rejected = true; }
        Require(rejected, "different seed reused existing saves");
        seed["goal"] = { { "type", "triforceHunt" }, { "requiredPieces", 1 }, { "totalPieces", 2 } };
        seed["oot"]["placements"]["Trap Chest"] = "Knockoff Item";
        seed["oot"]["placements"]["Piece Chest"] = "Triforce Piece (OOT)";
        seed["foreign"].push_back({ { "checkGame", "oot" },
                                    { "checkName", "Trap Chest" },
                                    { "itemGame", "mm" },
                                    { "itemName", "Knockoff Item" },
                                    { "trap", true } });
        seed["mm"]["placements"]["Sword Chest"] = "Kokiri Sword";
        seed["foreign"].push_back({ { "checkGame", "mm" },
                                    { "checkName", "Sword Chest" },
                                    { "itemGame", "oot" },
                                    { "itemName", "Kokiri Sword" } });
        std::ofstream(seedPath) << seed;
        ComboSession mixed;
        mixed.Open(seedPath, folder / "mixed", 0);
        Require(mixed.ReportObtained("oot", "Trap Chest"), "trap pickup rejected");
        Require(nlohmann::json::parse(mixed.Deliveries("oot")).size() == 1, "trap did not affect finder");
        Require(nlohmann::json::parse(mixed.Deliveries("mm")).empty(), "trap was sent to its item game");
        Require(mixed.ReportObtained("oot", "Piece Chest"), "Triforce pickup rejected");
        Require(nlohmann::json::parse(mixed.Deliveries("mm")).at(0).at("item") == "Piece of the Triforce",
                "combined Triforce count was not mirrored");
        Require(!mixed.ReportSharedTiers("oot", { { "bows", 17 } }), "invalid shared tier was accepted");
        Require(mixed.ReportObtained("mm", "Sword Chest"), "chain source pickup rejected");
        Require(!mixed.HasListedItems("oot"), "a new session already knew a game's items");
        mixed.SetListedItems("oot", { "Kokiri Sword" });
        Require(nlohmann::json::parse(mixed.Deliveries("oot")).at(1).at("item") == "Kokiri Sword",
                "a chain source was renamed without its pickup being listed");
        mixed.SetListedItems("oot", { "Kokiri Sword", "nei.progressive.kokiri_sword" });
        Require(nlohmann::json::parse(mixed.Deliveries("oot")).at(1).at("item") == "nei.progressive.kokiri_sword",
                "a chain source did not arrive as its listed pickup");
        seed["goal"] = { { "type", "bosses" }, { "mode", "both" }, { "pieces", 2 } };
        std::ofstream(seedPath) << seed;
        ComboSession triggers;
        triggers.Open(seedPath, folder / "triggers", 0);
        Require(triggers.ReportObtained("oot", "Piece Chest"), "trigger piece pickup rejected");
        Require(nlohmann::json::parse(triggers.Deliveries("mm")).at(0).at("item") == "Piece of the Triforce",
                "trigger pieces were not counted in both games");
        VerifyMaskAliases(folder);
        std::filesystem::remove_all(folder);
        std::cout << "Fleet session tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        std::filesystem::remove_all(folder);
        return 1;
    }
}
