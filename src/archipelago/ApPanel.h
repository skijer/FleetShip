#pragma once

#include <array>
#include <filesystem>
#include <string>

#include <imgui.h>

#include "ApExportActions.h"
#include "ApSession.h"
#include "host/GameInstall.h"
#include "host/SeedJob.h"

// The chooser's Archipelago page: picks what the slot plays, exports it, then joins the slot and readies its session.
class ApPanel {
  public:
    static constexpr int kStay = -2;
    static constexpr int kBack = -1;
    // Starts the combo with its randomizer options open, where the combo seed is exported.
    static constexpr int kSetUpCombo = 3;

    ApPanel(ApSession& session, UpdateFolders folders);
    // Returns the game mode to start once the slot is ready (0 combo, 1 OoT, 2 MM), kSetUpCombo, kBack or kStay.
    int Draw(const ImVec4& theme);
    std::string Slot() const {
        return mSlot.data();
    }

  private:
    bool DrawModeChoice(const ImVec4& theme);
    void DrawExport(const ImVec4& theme, bool& isComboSetUp);
    void ExportSolo(const char* game);
    void LoadConnection();
    void SaveConnection() const;

    ApSession& mSession;
    UpdateFolders mFolders;
    bool mHasOot;
    bool mHasMm;
    int mMode = 0;
    SeedJob mExport;
    ApExportActions mActions;
    std::string mExportStatus;
    std::array<char, 128> mServer = {};
    std::array<char, 17> mSlot = {};
    std::array<char, 64> mPassword = {};
};
