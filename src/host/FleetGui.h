#pragma once
#include <fast/Fast3dGui.h>
#include <fleet/FleetGameApi.h>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
class FleetSaveMenu;

class FleetGui : public Fast::Fast3dGui {
  public:
    using Windows = std::map<std::string, std::shared_ptr<Ship::GuiWindow>>;
    FleetGui();
    void Capture(const std::string& game);
    void Activate(const std::string& game);
    void SetReset(std::function<void()> callback);
    bool ExerciseMenu(const std::string& game);
    bool ExerciseRandomizerPages();
    void
    SetNativeRequest(std::function<std::string(const std::string&, const std::string&, const std::string&)> request);
    void SetGenerator(std::shared_ptr<FleetSaveMenu> generator);
    struct GivableItem {
        std::string game;
        std::string name;
        std::string label;
    };
    using Items = std::vector<GivableItem>;
    using ItemList = std::function<Items()>;
    using ItemGiver = std::function<bool(const std::string& game, const std::string& item)>;
    using ItemIconFinder = std::function<bool(const std::string& game, const std::string& item, FleetItemIcon& icon)>;
    struct ItemTools {
        ItemList list;
        ItemGiver give;
        ItemIconFinder findIcon;
        std::function<std::string()> giveAll;
        std::function<std::string()> drawTest;
    };
    void SetItemTools(ItemTools tools);
    void OpenRandomizer();
    void SetLoading(const std::string& text);
    struct ReviewPrompt {
        std::string game;
        std::string item;
        std::string text;
        ItemIconFinder findIcon;
    };
    void SetReviewPrompt(const std::string& game, const std::string& item, const std::string& text);
    int ChooseGame(int current, int automation = -1);

  private:
    class Menu;
    struct Child {
        Windows windows;
        std::shared_ptr<Ship::GuiWindow> menu;
        std::shared_ptr<Ship::GuiMenuBar> bar;
        ImGuiStyle style;
    };
    Windows mDefaults;
    std::map<std::string, Child> mChildren;
    std::shared_ptr<Menu> mFleetMenu;
    std::shared_ptr<std::string> mLoadingText = std::make_shared<std::string>();
    std::shared_ptr<Ship::GuiWindow> mLoadingOverlay;
    std::shared_ptr<ReviewPrompt> mReviewPrompt = std::make_shared<ReviewPrompt>();
    std::shared_ptr<Ship::GuiWindow> mReviewOverlay;
};
