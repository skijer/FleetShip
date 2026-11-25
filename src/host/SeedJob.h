#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
inline constexpr const char* kFleetExecutable = "fleet.exe";
#else
inline constexpr const char* kFleetExecutable = "fleet";
#endif

class SeedJob {
  public:
    ~SeedJob();
    void Start(const std::filesystem::path& executable, const std::vector<std::string>& arguments,
               const std::filesystem::path& log);
    std::optional<int> Poll();
    void Cancel();
    bool Running() const;

  private:
    void* mProcess = nullptr;
    void* mJob = nullptr;
    int mPid = -1;
};
