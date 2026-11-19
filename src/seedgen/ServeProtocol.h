#pragma once

#include <filesystem>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace ServeProtocol {

inline constexpr const char* kRequestFile = "request.json";
inline constexpr const char* kReplyFile = "reply.json";
inline constexpr const char* kReadyFile = "ready.json";

inline bool TryWriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << value.dump();
        if (!stream) {
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    return !error;
}

inline nlohmann::json ReadJson(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return stream ? nlohmann::json::parse(stream, nullptr, false) : nlohmann::json();
}

} // namespace ServeProtocol
