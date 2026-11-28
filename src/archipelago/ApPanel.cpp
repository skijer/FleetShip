#include "ApPanel.h"
#include "ApPaths.h"

#include <cstring>
#include <fstream>
#include <random>
#include <vector>

#include <nlohmann/json.hpp>

#include "gui/ComboWidgetStyle.h"

namespace {

constexpr const char* kConnectionFile = "connection.json";
constexpr const char* kExportLog = "export.log";

template <size_t Size> void CopyInto(std::array<char, Size>& target, const std::string& text) {
    target.fill('\0');
    std::strncpy(target.data(), text.c_str(), Size - 1);
}

int ModeIndex(const std::string& mode) {
    if (mode == "oot") {
        return 1;
    }
    return mode == "mm" ? 2 : 0;
}

} // namespace

ApPanel::ApPanel(ApSession& session, UpdateFolders folders)
    : mSession(session), mFolders(std::move(folders)), mHasOot(IsGameInstalled(mFolders.oot, 0)),
      mHasMm(IsGameInstalled(mFolders.mm, 1)) {
    CopyInto(mServer, "archipelago.gg:38281");
    LoadConnection();
}

void ApPanel::LoadConnection() {
    std::ifstream stream(ArchipelagoFolder(mFolders.fleet) / kConnectionFile);
    const nlohmann::json saved = stream ? nlohmann::json::parse(stream, nullptr, false) : nlohmann::json();
    if (saved.is_object()) {
        CopyInto(mServer, saved.value("server", std::string(mServer.data())));
        CopyInto(mSlot, saved.value("slot", std::string{}));
    }
}

void ApPanel::SaveConnection() const {
    std::filesystem::create_directories(ArchipelagoFolder(mFolders.fleet));
    std::ofstream(ArchipelagoFolder(mFolders.fleet) / kConnectionFile)
        << nlohmann::json{ { "server", mServer.data() }, { "slot", mSlot.data() } }.dump(2);
}

void ApPanel::ExportSolo(const char* game) {
    const std::filesystem::path folder = ArchipelagoFolder(mFolders.fleet);
    std::filesystem::create_directories(folder);
    const std::vector<std::string> arguments = { "--internal-worker",
                                                 "--oot",
                                                 mFolders.oot.string(),
                                                 "--mm",
                                                 mFolders.mm.string(),
                                                 "--seed",
                                                 std::to_string(std::random_device{}()),
                                                 "--solo",
                                                 game,
                                                 "--archipelago-out",
                                                 folder.string(),
                                                 "--archipelago-player",
                                                 mSlot.data() };
    mExport.Start(mFolders.fleet / kFleetExecutable, arguments, folder / kExportLog);
    mExportStatus = "Exporting... Fleet measures the logic of every check, which takes a few minutes.";
    mActions.Withdraw();
    SaveConnection();
}

namespace {

struct ModeCard {
    const char* title;
    const char* detail;
    const char* game;
};

const ModeCard kModeCards[] = {
    { "Combo", "OoT + MM", nullptr },
    { "Ocarina of Time", "on its own", "oot" },
    { "Majora's Mask", "on its own", "mm" },
};

const char* kModeHints[] = {
    "Opens the combo randomizer options. Choose them, then press Export for Archipelago there.",
    "Uses the randomizer settings saved in Ship of Harkinian.",
    "Uses the randomizer settings saved in 2 Ship 2 Harkinian.",
};

} // namespace

bool ApPanel::DrawModeChoice(const ImVec4& theme) {
    const bool isModePlayable[] = { mHasOot && mHasMm, mHasOot, mHasMm };
    if (!isModePlayable[mMode]) {
        mMode = mHasOot ? 1 : 2;
    }
    const float width = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
    for (int mode = 0; mode < 3; mode++) {
        if (mode > 0) {
            ImGui::SameLine();
        }
        const bool isChosen = mode == mMode;
        ImGui::PushStyleColor(ImGuiCol_Button, isChosen ? theme : ImVec4(0.18f, 0.18f, 0.22f, 1));
        ImGui::BeginDisabled(!isModePlayable[mode]);
        const std::string label =
            std::string(kModeCards[mode].title) + "\n" + kModeCards[mode].detail + "##apMode" + std::to_string(mode);
        if (ImGui::Button(label.c_str(), ImVec2(width, 56))) {
            mMode = mode;
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor();
    }
    ImGui::TextWrapped("%s", kModeHints[mMode]);
    return isModePlayable[mMode];
}

void ApPanel::DrawExport(const ImVec4& theme, bool& isComboSetUp) {
    const bool hasSlot = mSlot[0] != '\0';
    ComboRando::ComboMenu_PushButton(theme);
    ImGui::BeginDisabled(!hasSlot || mExport.Running());
    const char* label = mMode == 0 ? "Set up the combo seed" : "Export for Archipelago";
    if (ImGui::Button(label, ImVec2(-1, 40))) {
        if (mMode == 0) {
            SaveConnection();
            isComboSetUp = true;
        } else {
            ExportSolo(kModeCards[mMode].game);
        }
    }
    ImGui::EndDisabled();
    ComboRando::ComboMenu_PopButton();
    if (!hasSlot) {
        ImGui::TextDisabled("Type your slot name first.");
    }
    if (!mExportStatus.empty()) {
        ImGui::TextWrapped("%s", mExportStatus.c_str());
    }
    mActions.Draw(theme);
}

int ApPanel::Draw(const ImVec4& theme) {
    mSession.Pump(nullptr);
    const std::filesystem::path folder = ArchipelagoFolder(mFolders.fleet);
    if (const auto result = mExport.Poll()) {
        if (*result == 0) {
            mExportStatus.clear();
            mActions.Offer(folder, mSlot.data());
        } else {
            mExportStatus = "The export failed; see " + (folder / kExportLog).string() + ".";
        }
    }
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextColored(theme, "Archipelago");
    ImGui::SetWindowFontScale(1);
    ImGui::Separator();
    ComboRando::ComboMenu_PushInput(theme);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##apSlot", "Slot name (16 characters at most)", mSlot.data(), mSlot.size());
    ComboRando::ComboMenu_PopInput();
    const bool hasSlot = mSlot[0] != '\0';

    ImGui::Spacing();
    ImGui::TextUnformatted("1. What will you play?");
    bool isComboSetUp = false;
    if (DrawModeChoice(theme)) {
        DrawExport(theme, isComboSetUp);
    }
    if (isComboSetUp) {
        return kSetUpCombo;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("2. Join the multiworld");
    ComboRando::ComboMenu_PushInput(theme);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##apServer", "archipelago.gg:38281", mServer.data(), mServer.size());
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##apPassword", "Password (if the room has one)", mPassword.data(), mPassword.size(),
                             ImGuiInputTextFlags_Password);
    ComboRando::ComboMenu_PopInput();
    const ApSession::Stage stage = mSession.GetStage();
    const bool isBusy = stage == ApSession::Stage::Connecting || stage == ApSession::Stage::Preparing;
    ComboRando::ComboMenu_PushButton(theme);
    ImGui::BeginDisabled(!hasSlot || mServer[0] == '\0' || isBusy);
    if (ImGui::Button("Connect and Play", ImVec2(-1, 40))) {
        SaveConnection();
        mSession.Connect({ mServer.data(), mSlot.data(), mPassword.data() });
    }
    ImGui::EndDisabled();
    if (!mSession.Status().empty()) {
        ImGui::TextWrapped("%s", mSession.Status().c_str());
    }
    ImGui::Spacing();
    int result = kStay;
    if (ImGui::Button("Back", ImVec2(-1, 0))) {
        result = kBack;
    }
    ComboRando::ComboMenu_PopButton();
    if (stage != ApSession::Stage::Ready) {
        return result;
    }
    const int mode = ModeIndex(mSession.Mode());
    const bool isModePlayable[] = { mHasOot && mHasMm, mHasOot, mHasMm };
    if (!isModePlayable[mode]) {
        ImGui::TextWrapped("This slot needs a game that is not installed beside Fleet.");
        return result;
    }
    return mode;
}
