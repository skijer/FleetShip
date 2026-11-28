#pragma once

#include <cctype>
#include <filesystem>
#include <string>

// Exports, the apworld, YAMLs and Archipelago sessions all live in <Fleet install>/archipelago.
inline std::filesystem::path ArchipelagoFolder(const std::filesystem::path& install) {
    return install / "archipelago";
}

// A slot name as a file name: letters, digits, '-' and '_'; anything else becomes '_'.
inline std::string FileSafe(const std::string& text) {
    std::string safe;
    for (char character : text) {
        const bool isPlain =
            std::isalnum(static_cast<unsigned char>(character)) || character == '-' || character == '_';
        safe += isPlain ? character : '_';
    }
    return safe.empty() ? "slot" : safe;
}

// libzip and the player's messages take UTF-8; path.string() is the ANSI code page on Windows.
inline std::string ToUtf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}
