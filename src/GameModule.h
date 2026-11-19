#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <fleet/FleetGameApi.h>

struct GameFunctions {
    FleetNameFn Name = nullptr;
    FleetAttachFn Attach = nullptr;
    FleetRunFn Run = nullptr;
    FleetArriveFn Arrive = nullptr;
    FleetDetachFn Detach = nullptr;
    FleetFreeStringFn FreeString = nullptr;
    FleetDumpRandoStaticDataFn DumpRandoStaticData = nullptr;
    FleetRandoResetFn RandoReset = nullptr;
    FleetRandoSetOwnedItemsFn RandoSetOwnedItems = nullptr;
    FleetRandoGetReachableChecksFn RandoGetReachableChecks = nullptr;
    FleetRandoPlaceItemFn RandoPlaceItem = nullptr;
    FleetRandoRequestFn RandoRequest = nullptr;
    FleetDrawItemFn DrawItem = nullptr;
    FleetResetFn Reset = nullptr;
    FleetItemIconFn ItemIcon = nullptr;
};

class GameModule {
  public:
    static std::unique_ptr<GameModule> Load(const std::filesystem::path& installDirectory,
                                            const std::string& moduleBaseName, FleetGetFunctionFn hostFunction);
    ~GameModule();
    GameModule(const GameModule&) = delete;
    GameModule& operator=(const GameModule&) = delete;

    const GameFunctions& Functions() const;
    // Every call into the game must run in a Ship::GameScope of this name.
    const char* Name() const;
    std::string TakeString(char* text) const;
    std::string Request(const std::string& op, const std::string& json = "{}") const;

  private:
    GameModule(void* handle, GameFunctions functions, std::string installDirectory);

    void* mHandle;
    GameFunctions mFunctions;
    std::string mInstallDirectory;
};
