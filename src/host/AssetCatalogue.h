#pragma once

#include <array>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include <fast/resource/type/Texture.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>

// Reads what a game hands Fleet for an item, so a regression can compare it run to run: the icon texture as pixels,
// and the display list as a fingerprint built from resource names and contents, never from memory addresses.
namespace AssetCatalogue {

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

inline uint64_t MixBytes(uint64_t hash, const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * kFnvPrime;
    }
    return hash;
}

inline uint64_t MixValue(uint64_t hash, uint64_t value) {
    return MixBytes(hash, &value, sizeof(value));
}

inline uint64_t MixText(uint64_t hash, const std::string& text) {
    return MixBytes(hash, text.data(), text.size());
}

inline std::string HexOf(uint64_t value) {
    std::ostringstream text;
    text << std::hex << std::setw(16) << std::setfill('0') << value;
    return text.str();
}

inline std::string FileNameOf(const std::string& item) {
    std::string name;
    for (const char character : item) {
        name += std::isalnum(static_cast<unsigned char>(character)) ? character : '_';
    }
    return name.substr(0, 48) + "_" + HexOf(MixText(kFnvOffset, item)).substr(0, 8);
}

struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba;
};

inline uint8_t Expand(uint32_t value, uint32_t bits) {
    return static_cast<uint8_t>(value * 255 / ((1u << bits) - 1));
}

// Palette formats need their TLUT resource, which the icon getter does not name.
inline std::optional<Image> DecodeTexture(const Fast::Texture& texture) {
    const auto type = texture.Type;
    const uint32_t bitsPerPixel = type == Fast::TextureType::RGBA32bpp             ? 32
                                  : type == Fast::TextureType::RGBA16bpp           ? 16
                                  : type == Fast::TextureType::GrayscaleAlpha16bpp ? 16
                                  : type == Fast::TextureType::Grayscale8bpp       ? 8
                                  : type == Fast::TextureType::GrayscaleAlpha8bpp  ? 8
                                  : type == Fast::TextureType::Grayscale4bpp       ? 4
                                  : type == Fast::TextureType::GrayscaleAlpha4bpp  ? 4
                                                                                   : 0;
    const uint32_t height = static_cast<uint32_t>(texture.Height * texture.VPixelScale);
    if (bitsPerPixel == 0 || texture.ImageData == nullptr || height == 0) {
        return std::nullopt;
    }
    Image image;
    image.height = height;
    image.width = texture.ImageDataSize * 8 / bitsPerPixel / height;
    image.rgba.reserve(static_cast<size_t>(image.width) * height * 4);
    const uint8_t* data = texture.ImageData;
    for (uint32_t pixel = 0; pixel < image.width * height; pixel++) {
        std::array<uint8_t, 4> colour = {};
        if (bitsPerPixel == 32) {
            std::memcpy(colour.data(), data + pixel * 4, 4);
        } else if (bitsPerPixel == 16) {
            const uint32_t value = (data[pixel * 2] << 8) | data[pixel * 2 + 1];
            colour = type == Fast::TextureType::RGBA16bpp
                         ? std::array<uint8_t, 4>{ Expand(value >> 11, 5), Expand((value >> 6) & 31, 5),
                                                   Expand((value >> 1) & 31, 5),
                                                   static_cast<uint8_t>((value & 1) * 255) }
                         : std::array<uint8_t, 4>{ static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 8),
                                                   static_cast<uint8_t>(value >> 8),
                                                   static_cast<uint8_t>(value & 255) };
        } else if (bitsPerPixel == 8) {
            const uint8_t value = data[pixel];
            const bool hasAlpha = type == Fast::TextureType::GrayscaleAlpha8bpp;
            const uint8_t grey = hasAlpha ? Expand(value >> 4, 4) : value;
            colour = { grey, grey, grey, hasAlpha ? Expand(value & 15, 4) : uint8_t{ 255 } };
        } else {
            const uint8_t value = (pixel % 2 == 0) ? data[pixel / 2] >> 4 : data[pixel / 2] & 15;
            const bool hasAlpha = type == Fast::TextureType::GrayscaleAlpha4bpp;
            const uint8_t grey = hasAlpha ? Expand(value >> 1, 3) : Expand(value, 4);
            colour = { grey, grey, grey, hasAlpha ? static_cast<uint8_t>((value & 1) * 255) : uint8_t{ 255 } };
        }
        image.rgba.insert(image.rgba.end(), colour.begin(), colour.end());
    }
    return image;
}

inline uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

inline void PutBigEndian(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>(value >> shift));
    }
}

inline void PutChunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
    PutBigEndian(png, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> typed(type, type + 4);
    typed.insert(typed.end(), data.begin(), data.end());
    png.insert(png.end(), typed.begin(), typed.end());
    PutBigEndian(png, Crc32(typed.data(), typed.size()));
}

// An uncompressed PNG: icons are small and this keeps Fleet free of an image library.
inline bool WritePng(const std::filesystem::path& path, const Image& image) {
    std::vector<uint8_t> rows;
    for (uint32_t y = 0; y < image.height; y++) {
        rows.push_back(0);
        const auto* row = image.rgba.data() + static_cast<size_t>(y) * image.width * 4;
        rows.insert(rows.end(), row, row + image.width * 4);
    }
    std::vector<uint8_t> zlib = { 0x78, 0x01 };
    uint32_t adlerA = 1;
    uint32_t adlerB = 0;
    for (size_t offset = 0; offset < rows.size() || offset == 0; offset += 65535) {
        const size_t length = std::min<size_t>(65535, rows.size() - offset);
        const bool isLast = offset + length >= rows.size();
        zlib.push_back(isLast ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(length));
        zlib.push_back(static_cast<uint8_t>(length >> 8));
        zlib.push_back(static_cast<uint8_t>(~length));
        zlib.push_back(static_cast<uint8_t>(~length >> 8));
        zlib.insert(zlib.end(), rows.begin() + offset, rows.begin() + offset + length);
        for (size_t i = offset; i < offset + length; i++) {
            adlerA = (adlerA + rows[i]) % 65521;
            adlerB = (adlerB + adlerA) % 65521;
        }
        if (isLast) {
            break;
        }
    }
    PutBigEndian(zlib, (adlerB << 16) | adlerA);
    std::vector<uint8_t> header;
    PutBigEndian(header, image.width);
    PutBigEndian(header, image.height);
    header.insert(header.end(), { 8, 6, 0, 0, 0 });
    std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    PutChunk(png, "IHDR", header);
    PutChunk(png, "IDAT", zlib);
    PutChunk(png, "IEND", {});
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return file.good();
}

// A display list can hold words that only look like pointers; nothing is read unless the memory is there.
inline bool IsReadable(uintptr_t address, size_t size) {
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION region = {};
    if (size == 0 || VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) == 0) {
        return false;
    }
    constexpr DWORD kReadable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
    return region.State == MEM_COMMIT && (region.Protect & kReadable) != 0 && (region.Protect & PAGE_GUARD) == 0 &&
           address + size <= regionEnd;
#else
    return false;
#endif
}

// Resource paths are printable and contain a folder separator; anything else is not a path.
inline const char* ReadPath(uintptr_t address) {
    constexpr size_t kLongestPath = 256;
    for (size_t length = 0; length < kLongestPath; length++) {
        if (!IsReadable(address + length, 1)) {
            return nullptr;
        }
        const char character = *reinterpret_cast<const char*>(address + length);
        if (character == '\0') {
            const auto* path = reinterpret_cast<const char*>(address);
            return length > 0 && std::strchr(path, '/') != nullptr ? path : nullptr;
        }
        if (character < 0x20 || character > 0x7E) {
            return nullptr;
        }
    }
    return nullptr;
}

struct Command {
    uintptr_t w0;
    uintptr_t w1;
};
static_assert(sizeof(Command) == 16, "libultraship Gfx is two pointer-sized words");

struct DlSummary {
    uint64_t fingerprint = kFnvOffset;
    size_t commands = 0;
    std::set<std::string> resources;
    std::set<std::string> issues;
};

// Fingerprints a display list by what it draws: resource names for OTR references, contents for raw vertices and
// matrices, values for immediate commands. Addresses never enter the fingerprint, so it is stable between runs.
class DlWalker {
  public:
    explicit DlWalker(std::shared_ptr<Ship::ResourceManager> resources) : mResources(std::move(resources)) {
    }

    DlSummary Walk(const Command* begin, const Command* end) {
        WalkList(begin, end, 0);
        return mSummary;
    }

  private:
    // F3DEX2 and libultraship opcodes, by value: the gbi headers define them per microcode.
    static constexpr uint8_t kVtx = 0x01;
    static constexpr uint8_t kSetTimgOtrHash = 0x20;
    static constexpr uint8_t kVtxOtrFilepath = 0x24;
    static constexpr uint8_t kSetTimgOtrFilepath = 0x25;
    static constexpr uint8_t kDlOtrFilepath = 0x27;
    static constexpr uint8_t kMtxOtrFilepath = 0x29;
    static constexpr uint8_t kDlOtrHash = 0x31;
    static constexpr uint8_t kVtxOtrHash = 0x32;
    static constexpr uint8_t kMarker = 0x33;
    static constexpr uint8_t kBranchZOtr = 0x35;
    static constexpr uint8_t kMtxOtr = 0x36;
    static constexpr uint8_t kTexRectWide = 0x37;
    static constexpr uint8_t kFillWideRect = 0x38;
    static constexpr uint8_t kImageRect = 0x3C;
    static constexpr uint8_t kReadFb = 0x3E;
    static constexpr uint8_t kRegBlendedTex = 0x3F;
    static constexpr uint8_t kMtx = 0xDA;
    static constexpr uint8_t kMoveMem = 0xDC;
    static constexpr uint8_t kDl = 0xDE;
    static constexpr uint8_t kEndDl = 0xDF;
    static constexpr int kMostDepth = 32;
    static constexpr size_t kMostCommands = 200000;

    static bool IsSegmented(uintptr_t address) {
        return address <= 0xFFFFFFFFu && (address & 1) != 0;
    }

    static bool IsHostPointer(uintptr_t address) {
        return address >= 0x10000 && address <= 0x0000FFFFFFFFFFFFull && !IsSegmented(address);
    }

    void Mix(uint64_t value) {
        mSummary.fingerprint = MixValue(mSummary.fingerprint, value);
    }

    void MixContent(uintptr_t address, size_t size) {
        if (IsHostPointer(address) && IsReadable(address, size)) {
            mSummary.fingerprint = MixBytes(mSummary.fingerprint, reinterpret_cast<const void*>(address), size);
        } else {
            Mix(address);
        }
    }

    void Note(const char* name, const std::string& fallback) {
        if (name == nullptr) {
            mSummary.issues.insert(fallback);
            return;
        }
        mSummary.resources.insert(name);
        mSummary.fingerprint = MixText(mSummary.fingerprint, name);
    }

    const char* NameOfHash(uint64_t hash) {
        return mResources->GetArchiveManager()->HashToCString(hash);
    }

    void Enter(const void* list, int depth, const std::string& what) {
        if (list == nullptr) {
            mSummary.issues.insert("unresolved " + what);
            return;
        }
        if (!IsReadable(reinterpret_cast<uintptr_t>(list), sizeof(Command))) {
            mSummary.issues.insert("unreadable " + what);
            return;
        }
        if (mVisited.insert(list).second) {
            WalkList(static_cast<const Command*>(list), nullptr, depth + 1);
        }
    }

    // A null end walks until G_ENDDL; a branching G_DL ends the list it is in.
    void WalkList(const Command* command, const Command* end, int depth) {
        if (depth > kMostDepth) {
            mSummary.issues.insert("display lists nest deeper than " + std::to_string(kMostDepth));
            return;
        }
        while ((end == nullptr || command < end) && mSummary.commands < kMostCommands) {
            mSummary.commands++;
            const uint8_t opcode = static_cast<uint8_t>(command->w0 >> 24);
            Mix(opcode);
            if (opcode == kEndDl && end == nullptr) {
                return;
            }
            const bool branches = ((command->w0 >> 16) & 0xFF) != 0;
            const Command* next = Step(command, opcode, depth);
            if ((opcode == kDl || opcode == kDlOtrHash || opcode == kDlOtrFilepath) && branches) {
                return;
            }
            command = next;
        }
    }

    const Command* Step(const Command* command, uint8_t opcode, int depth) {
        switch (opcode) {
            case kSetTimgOtrHash:
            case kVtxOtrHash:
            case kMtxOtr:
            case kDlOtrHash: {
                const Command* hashed = command + 1;
                const uint64_t hash = (static_cast<uint64_t>(hashed->w0) << 32) + hashed->w1;
                Mix(command->w0 & 0xFFFFFF);
                Note(NameOfHash(hash), "unknown resource hash");
                if (opcode == kDlOtrHash) {
                    Enter(mResources->GetResourceRawPointer(hash), depth, "display list hash");
                }
                return command + 2;
            }
            case kVtxOtrFilepath:
            case kSetTimgOtrFilepath:
            case kMtxOtrFilepath:
            case kDlOtrFilepath: {
                const char* path = IsHostPointer(command->w1) ? ReadPath(command->w1) : nullptr;
                Mix(command->w0 & 0xFFFFFF);
                Note(path, "unreadable resource path");
                if (opcode == kDlOtrFilepath && path != nullptr) {
                    Enter(mResources->GetResourceRawPointer(path), depth, std::string("display list ") + path);
                }
                return command + (opcode == kVtxOtrFilepath ? 2 : 1);
            }
            case kMarker:
            case kBranchZOtr:
            case kFillWideRect:
            case kReadFb:
            case kRegBlendedTex:
            case kTexRectWide:
            case kImageRect: {
                const size_t words = opcode == kTexRectWide || opcode == kImageRect ? 3 : 2;
                for (size_t word = 0; word < words; word++) {
                    Mix(command[word].w0);
                    Mix(IsHostPointer(command[word].w1) ? 0 : command[word].w1);
                }
                return command + words;
            }
            case kDl:
                if (IsSegmented(command->w1)) {
                    Mix(command->w1);
                    mSummary.resources.insert("segment " + std::to_string((command->w1 >> 24) & 0x0F));
                } else {
                    Enter(IsHostPointer(command->w1) ? reinterpret_cast<const void*>(command->w1) : nullptr, depth,
                          "display list pointer");
                }
                return command + 1;
            case kVtx:
                Mix(command->w0);
                MixContent(command->w1, ((command->w0 >> 12) & 0xFF) * 16);
                return command + 1;
            case kMtx:
                Mix(command->w0);
                MixContent(command->w1, 64);
                return command + 1;
            case kMoveMem:
                Mix(command->w0);
                MixContent(command->w1, ((command->w0 >> 19) & 0x1F) * 8 + 8);
                return command + 1;
            default:
                Mix(command->w0);
                Mix(IsHostPointer(command->w1) ? 0 : command->w1);
                return command + 1;
        }
    }

    std::shared_ptr<Ship::ResourceManager> mResources;
    std::set<const void*> mVisited;
    DlSummary mSummary;
};

} // namespace AssetCatalogue
