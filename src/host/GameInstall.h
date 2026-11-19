#pragma once

#include <filesystem>
#include <string>

bool EnsureGameInstalls(const std::filesystem::path& folder, std::string& oot, std::string& mm);
