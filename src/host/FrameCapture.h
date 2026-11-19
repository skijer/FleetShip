#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include <fast/Fast3dWindow.h>
#include <fast/backends/gfx_rendering_api.h>
#include <fast/interpreter.h>
#include <ship/Context.h>

namespace FrameCapture {

inline void AppendColour(std::vector<char>& bytes, uint16_t pixel) {
    constexpr std::array<int, 3> kChannelShifts = { 11, 6, 1 };
    constexpr uint32_t kChannelMost = 31;
    for (const int shift : kChannelShifts) {
        const uint32_t channel = (pixel >> shift) & kChannelMost;
        bytes.push_back(static_cast<char>(channel * 255 / kChannelMost));
    }
}

inline bool Save(const std::filesystem::path& path) {
    const auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    const auto interpreter = window != nullptr ? window->GetInterpreterWeak().lock() : nullptr;
    if (interpreter == nullptr || !interpreter->mRendersToFb) {
        return false;
    }
    const uint32_t width = interpreter->mCurDimensions.width;
    const uint32_t height = interpreter->mCurDimensions.height;
    const int source = interpreter->mMsaaLevel > 1 ? interpreter->mGameFbMsaaResolved : interpreter->mGameFb;
    std::vector<uint16_t> pixels(static_cast<size_t>(width) * height);
    interpreter->mRapi->ReadFramebufferToCPU(source, width, height, pixels.data());
    std::vector<char> bytes;
    bytes.reserve(pixels.size() * 3);
    for (const uint16_t pixel : pixels) {
        AppendColour(bytes, pixel);
    }
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << width << " " << height << "\n255\n";
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

} // namespace FrameCapture
