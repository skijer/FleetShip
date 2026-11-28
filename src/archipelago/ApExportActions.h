#pragma once

#include <filesystem>
#include <string>

#include <imgui.h>

// What to do with a finished export: install it in this computer's Archipelago, or zip it for the host.
class ApExportActions {
  public:
    void Offer(std::filesystem::path exportFolder, std::string slot);
    void Withdraw();
    void Draw(const ImVec4& theme);

  private:
    std::filesystem::path mExportFolder;
    std::string mSlot;
    std::string mStatus;
};
