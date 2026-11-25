#pragma once

#include <optional>

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

struct Frame {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<char> rgb;
};

// Needs the game to render into its own framebuffer (low-resolution mode); otherwise there is nothing to read.
inline std::optional<Frame> Grab() {
    const auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetRawInstance()->GetWindow());
    const auto interpreter = window != nullptr ? window->GetInterpreterWeak().lock() : nullptr;
    if (interpreter == nullptr || !interpreter->mRendersToFb) {
        return std::nullopt;
    }
    Frame frame = { interpreter->mCurDimensions.width, interpreter->mCurDimensions.height, {} };
    const int source = interpreter->mMsaaLevel > 1 ? interpreter->mGameFbMsaaResolved : interpreter->mGameFb;
    std::vector<uint16_t> pixels(static_cast<size_t>(frame.width) * frame.height);
    interpreter->mRapi->ReadFramebufferToCPU(source, frame.width, frame.height, pixels.data());
    frame.rgb.reserve(pixels.size() * 3);
    for (const uint16_t pixel : pixels) {
        AppendColour(frame.rgb, pixel);
    }
    return frame;
}

inline bool Save(const std::filesystem::path& path) {
    const std::optional<Frame> frame = Grab();
    if (!frame) {
        return false;
    }
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << frame->width << " " << frame->height << "\n255\n";
    file.write(frame->rgb.data(), static_cast<std::streamsize>(frame->rgb.size()));
    return file.good();
}

} // namespace FrameCapture
