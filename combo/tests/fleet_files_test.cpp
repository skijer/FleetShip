#include "FleetSaveMenu.h"
#include <chrono>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
void Require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void Put(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << value;
}
std::string Read(const fs::path& path) {
    std::ifstream stream(path);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}
int main() {
    const auto root = fs::temp_directory_path() /
                      ("fleet-files-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto session = root / "original";
        Put(root / "seed.json", "seed");
        Put(session / "seed.json", "seed");
        Put(session / "slot0" / "obtained.json", "journal");
        for (const auto* game : { "oot", "mm" }) {
            const auto folder = session / "slot0" / game / (std::string(game) == "oot" ? "Save" : "saves");
            const std::string extension = std::string(game) == "oot" ? ".sav" : ".json";
            Put(folder / ("file1" + extension), game);
            Put(folder / ("file1backup" + extension), std::string(game) + " backup");
        }
        Put(root / "slots.json", nlohmann::json({ { "0",
                                                    { { "seed", (root / "seed.json").string() },
                                                      { "session", session.string() },
                                                      { "number", "80302" } } } })
                                     .dump());
        FleetSaveMenu files(root, "oot", "mm", root);
        Require(files.Manage("copy", 0, 1), "copy was not handled");
        FleetSaveMenu reloaded(root, "oot", "mm", root);
        Require(reloaded.HasSlot(0) && reloaded.HasSlot(1), "copy registry was not durable");
        for (const auto* game : { "oot", "mm" }) {
            Require(Read(reloaded.SaveFile(game, 1, false)) == game, "copied native save differs");
            Require(Read(reloaded.SaveFile(game, 1, true)) == std::string(game) + " backup", "copied backup differs");
        }
        const auto slots = nlohmann::json::parse(Read(root / "slots.json"));
        Require(Read(fs::path(slots["1"]["session"].get<std::string>()) / "slot1" / "obtained.json") == "journal",
                "cross-game journal was not copied");
        bool rejected = false;
        try {
            reloaded.Manage("copy", 0, 1);
        } catch (...) { rejected = true; }
        Require(rejected, "copy overwrote an occupied slot");
        Require(reloaded.Manage("erase", 0, -1), "erase was not handled");
        FleetSaveMenu erased(root, "oot", "mm", root);
        Require(!erased.HasSlot(0) && erased.HasSlot(1), "erase registry was not durable");
        Require(Read(session / "slot0" / "obtained.json") == "journal", "erase destroyed recovery files");
        Require(Read(erased.SaveFile("mm", 1, false)) == "mm", "erase affected copied save");
        Put(erased.SaveFile("oot", 0, false), "story oot");
        Put(erased.SaveFile("mm", 0, false), "story mm");
        Require(erased.Manage("copy", 0, 2), "native quest copy was not handled");
        Require(Read(erased.SaveFile("oot", 2, false)) == "story oot", "native OoT quest was not copied");
        Require(Read(erased.SaveFile("mm", 2, false)) == "story mm", "native MM companion was not copied");
        Require(erased.Manage("erase", 0, -1), "native quest erase was not handled");
        Require(!fs::exists(erased.SaveFile("oot", 0, false)) && !fs::exists(erased.SaveFile("mm", 0, false)),
                "native paired quest remains visible after erase");
        fs::remove_all(root);
        std::cout << "paired native file management passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
