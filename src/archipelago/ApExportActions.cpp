#include "ApExportActions.h"
#include "ApInstall.h"

#include <utility>

#include "gui/ComboWidgetStyle.h"
#include "host/FileDialogs.h"

void ApExportActions::Offer(std::filesystem::path exportFolder, std::string slot) {
    mExportFolder = std::move(exportFolder);
    mSlot = std::move(slot);
    mStatus.clear();
}

void ApExportActions::Withdraw() {
    mSlot.clear();
    mStatus.clear();
}

void ApExportActions::Draw(const ImVec4& theme) {
    if (mSlot.empty()) {
        return;
    }
    ImGui::TextColored(theme, "%s's seed is ready. Who generates the multiworld?", mSlot.c_str());
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
    ComboRando::ComboMenu_PushButton(theme);
    if (ImGui::Button("I do: install it\nin my Archipelago", ImVec2(half, 48))) {
        mStatus = InstallIntoArchipelago(mExportFolder, mSlot);
    }
    ImGui::SameLine();
    if (ImGui::Button("Someone else: make\na .zip for the host", ImVec2(half, 48))) {
        const HostPackage package = PackageForHost(mExportFolder, mSlot);
        mStatus = package.message;
        if (!package.zip.empty()) {
            RevealInFileManager(package.zip);
        }
    }
    ComboRando::ComboMenu_PopButton();
    if (!mStatus.empty()) {
        ImGui::TextWrapped("%s", mStatus.c_str());
    }
}
