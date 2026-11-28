#pragma once

#include <filesystem>
#include <string>

// Native pickers; each returns an empty path when the player cancels. Patterns are separated by spaces: "*.z64 *.n64".
std::filesystem::path ChooseFile(const std::string& title, const std::string& filterName, const std::string& patterns);
std::filesystem::path ChooseFolder(const std::string& title);
// Opens the system file manager at the file, selecting it where the system can.
void RevealInFileManager(const std::filesystem::path& file);
