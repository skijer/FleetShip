#include "SettingsSync.h"
#include <ship/config/Config.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / "fleet-settings-test";
    std::filesystem::create_directories(root);
    auto a = Ship::GameRegistry::Register("oot"), b = Ship::GameRegistry::Register("mm");
    a->ConsoleVariables =
        std::make_shared<Ship::ConsoleVariable>(std::make_shared<Ship::Config>((root / "oot.json").string()));
    b->ConsoleVariables =
        std::make_shared<Ship::ConsoleVariable>(std::make_shared<Ship::Config>((root / "mm.json").string()));
    auto oot = a->ConsoleVariables, mm = b->ConsoleVariables;
    for (const auto& p : SettingsSync::Pairs()) {
        oot->ClearVariable(p.oot);
        mm->ClearVariable(p.mm);
    }
    for (const auto& p : SettingsSync::RandoPairs()) {
        oot->ClearVariable(p.oot);
        mm->ClearVariable(p.mm);
    }
    SettingsSync sync;
    int notifications = 0;
    auto apply = [&](const auto&, const auto&) { ++notifications; };
    sync.Poll("oot", apply);
    Require(notifications == 0, "untouched defaults must remain untouched");
    for (const auto& p : SettingsSync::Pairs()) {
        if (p.kind == SettingsSync::Kind::Text) {
            oot->SetString(p.oot, "fleet-room");
            sync.Poll("oot", apply);
            Require(std::string(mm->GetString(p.mm, "")) == "fleet-room", "OoT to MM text sync failed");
            mm->SetString(p.mm, "other-room");
            sync.Poll("mm", apply);
            Require(std::string(oot->GetString(p.oot, "")) == "other-room", "MM to OoT text sync failed");
            oot->ClearVariable(p.oot);
            sync.Poll("oot", apply);
            Require(!mm->Get(p.mm), "text reset did not propagate");
            continue;
        }
        if (p.kind == SettingsSync::Kind::Percent) {
            oot->SetInteger(p.oot, 50);
            sync.Poll("oot", apply);
            Require(mm->GetFloat(p.mm, -99) == 0.5f, "OoT percent did not reach MM as a fraction");
            mm->SetFloat(p.mm, 0.25f);
            sync.Poll("mm", apply);
            Require(oot->GetInteger(p.oot, -99) == 25, "MM fraction did not reach OoT as a percent");
            oot->ClearVariable(p.oot);
            sync.Poll("oot", apply);
            Require(!mm->Get(p.mm), "percent reset did not propagate");
            continue;
        }
        const float value = p.minimum != p.maximum ? p.maximum : 1;
        if (p.floating)
            oot->SetFloat(p.oot, value);
        else
            oot->SetInteger(p.oot, static_cast<int>(value));
        sync.Poll("oot", apply);
        Require(p.floating ? mm->GetFloat(p.mm, -99) == value : mm->GetInteger(p.mm, -99) == value,
                "OoT to MM sync failed");
        const float reverse = p.minimum != p.maximum ? p.minimum : 0;
        if (p.floating)
            mm->SetFloat(p.mm, reverse);
        else
            mm->SetInteger(p.mm, static_cast<int>(reverse));
        sync.Poll("mm", apply);
        Require(p.floating ? oot->GetFloat(p.oot, -99) == reverse : oot->GetInteger(p.oot, -99) == reverse,
                "MM to OoT sync failed");
        oot->ClearVariable(p.oot);
        sync.Poll("oot", apply);
        Require(!mm->Get(p.mm), "reset did not propagate");
    }
    oot->SetInteger("gRandoSettings.ShufflePots", 2);
    sync.Poll("oot", apply);
    Require(mm->GetInteger("gRando.Options.RO_SHUFFLE_POT_DROPS", -1) == 1,
            "OoT overworld pots did not turn MM pots on");
    mm->SetInteger("gRando.Options.RO_SHUFFLE_POT_DROPS", 0);
    sync.Poll("mm", apply);
    Require(oot->GetInteger("gRandoSettings.ShufflePots", -1) == 0, "MM pots off did not reach OoT");
    mm->SetInteger("gRando.Options.RO_SHUFFLE_POT_DROPS", 1);
    sync.Poll("mm", apply);
    Require(oot->GetInteger("gRandoSettings.ShufflePots", -1) == 3, "MM pots on did not mean all OoT pots");
    oot->SetInteger("gRandoSettings.Keysanity", 3);
    sync.Poll("oot", apply);
    Require(mm->GetInteger("gRando.Options.RO_PLACEMENT_SMALL_KEYS", -1) == 4,
            "any-dungeon keys are not MM any dungeon");
    oot->SetInteger("gRandoSettings.Keysanity", 4);
    sync.Poll("oot", apply);
    Require(mm->GetInteger("gRando.Options.RO_PLACEMENT_SMALL_KEYS", -1) == 5, "overworld keys are not MM overworld");
    mm->SetInteger("gRando.Options.RO_PLACEMENT_SMALL_KEYS", 1);
    sync.Poll("mm", apply);
    Require(oot->GetInteger("gRandoSettings.Keysanity", -1) == 2, "MM own dungeon is not OoT own dungeon");
    mm->SetInteger("gRando.Options.RO_LOGIC", 2);
    sync.Poll("mm", apply);
    Require(oot->GetInteger("gRandoSettings.NoLogic", -1) == 1, "MM nearly-no-logic is not OoT no logic");
    oot->SetInteger("gRandoSettings.ShuffleTokens", 1);
    sync.Poll("oot", apply);
    Require(mm->GetInteger("gRando.Options.RO_SHUFFLE_GOLD_SKULLTULAS", -1) == 1,
            "dungeon tokens did not shuffle MM spiders");
    oot->SetInteger("gRandoEnhancements.TimeSavers.SkipGetItemAnimation", 2);
    sync.Poll("oot", apply);
    Require(mm->GetInteger("gEnhancements.Cutscenes.SkipGetItemCutscenes", -1) == 3, "skip-all did not reach MM");
    oot->SetFloat("gSettings.FreeLook.CameraSensitivity.X", 2.5f);
    sync.Poll("oot", apply);
    Ship::ConsoleVariable persisted(std::make_shared<Ship::Config>((root / "mm.json").string()));
    Require(persisted.GetFloat("gEnhancements.Camera.RightStick.CameraSensitivity.X", 0) == 2.5f,
            "peer setting was not persisted");
    oot->SetFloat("gSettings.FirstPersonCameraSensitivity.X", 5);
    sync.Poll("oot", apply);
    Require(mm->GetFloat("gEnhancements.Camera.FirstPerson.SensitivityX", 0) == 2,
            "MM sensitivity range not respected");
    Require(oot->GetFloat("gSettings.FirstPersonCameraSensitivity.X", 0) == 2,
            "shared sensitivity disagrees after clamp");
    const int previous = notifications;
    sync.Poll("oot", apply);
    Require(notifications == previous, "unchanged settings reapply every frame");
    std::cout << "BIDIRECTIONAL_SETTINGS_PERSISTENCE_PASS\n";
}
