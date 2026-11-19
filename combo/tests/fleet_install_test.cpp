#define SDL_MAIN_HANDLED
#include "../../src/host/GameInstall.cpp"
#include <iostream>

void Require(bool value) {
    if (!value) {
        throw std::runtime_error("Installer verification failed");
    }
}

template <typename F> void RequireRejected(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) { rejected = true; }
    Require(rejected);
}

int main() {
    std::array<uint8_t, 32> seed{};
    seed[0] = 19;
    std::array<uint8_t, 32> publicKey{};
    std::array<uint8_t, 64> secretKey{};
    crypto_ed25519_key_pair(secretKey.data(), publicKey.data(), seed.data());
    GameInstall game = Games[0];
    game.key = publicKey.data();
    const std::string payload = R"({"format":1,"version":"test","assets":{}})";
    const std::string message = "unbound-release-v1\n" + payload;
    std::array<uint8_t, 64> signature{};
    crypto_ed25519_sign(signature.data(), secretKey.data(), reinterpret_cast<const uint8_t*>(message.data()),
                        message.size());
    std::string hex;
    for (uint8_t byte : signature) {
        hex += "0123456789abcdef"[byte >> 4];
        hex += "0123456789abcdef"[byte & 15];
    }
    auto encode = [](const Json& document) {
        const std::string text = document.dump();
        return std::vector<uint8_t>(text.begin(), text.end());
    };
    Json envelope = { { "payload", payload }, { "signature", hex } };
    Require(VerifyManifest(encode(envelope), game).at("version") == "test");
    envelope["payload"] = payload + " ";
    RequireRejected([&] { VerifyManifest(encode(envelope), game); });
    envelope["payload"] = payload;
    envelope["signature"] = "bad";
    RequireRejected([&] { VerifyManifest(encode(envelope), game); });
    RequireRejected([&] { VerifyManifest(encode(envelope), Games[0]); });
    RequireRejected([&] { DecodeHex(std::string(128, 'z'), 64); });
    const fs::path fixtureRoot = fs::temp_directory_path() / ("fleet-install-test-" + std::to_string(SDL_GetTicks64()));
    fs::create_directories(fixtureRoot);
    const auto makeZip = [&](const std::vector<std::string>& names) {
        const fs::path path = fixtureRoot / "fixture.zip";
        int error = 0;
        zip_t* archive = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
        Require(archive != nullptr);
        for (const auto& name : names) {
            zip_source_t* source = zip_source_buffer(archive, "test", 4, 0);
            Require(source != nullptr);
            Require(zip_file_add(archive, name.c_str(), source, ZIP_FL_ENC_UTF_8) >= 0);
        }
        Require(zip_close(archive) == 0);
        std::ifstream file(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
    };
    RequireRejected([&] { ExtractArchive(makeZip({ "../escape.dll" }), fixtureRoot / "unsafe", game); });
    Require(!fs::exists(fixtureRoot / "escape.dll"));
    RequireRejected([&] { ExtractArchive(makeZip({ "unrelated.txt" }), fixtureRoot / "incomplete", game); });
    const fs::path installed = fixtureRoot / "complete";
    ExtractArchive(makeZip({ std::string("package/") + game.module, std::string("package/") + game.archive }),
                   installed, game);
    Require(fs::is_regular_file(installed / game.module));
    Require(!HasGameFiles(installed, game));
    std::ofstream(installed / game.data) << "ROM fixture";
    Require(HasGameFiles(installed, game));
    fs::remove_all(fixtureRoot);
    std::cout << "PASS: signed payload accepted; tampering, malformed signature, unset key and malformed hash "
                 "rejected; ZIP traversal rejected; wrapped ZIP installed; ROM prerequisite enforced\n";
    return 0;
}
