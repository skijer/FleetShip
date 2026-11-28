#include "ApSocket.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <random>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define SECURITY_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <security.h>
#include <schannel.h>
#else
#include <dlfcn.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kNoSocket = INVALID_SOCKET;
constexpr int kSendFlags = 0;

void CloseSocket(SocketHandle socket) {
    closesocket(socket);
}

bool StartSockets() {
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return started;
}
#else
using SocketHandle = int;
constexpr SocketHandle kNoSocket = -1;
#ifdef MSG_NOSIGNAL
// A send to a server that already hung up must fail, not raise SIGPIPE and end Fleet.
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

void CloseSocket(SocketHandle socket) {
    close(socket);
}

bool StartSockets() {
    return true;
}
#endif

constexpr const char* kDefaultPort = "38281";
constexpr size_t kReadChunk = 16384;

SocketHandle ConnectTcp(const std::string& host, const std::string& port, std::string& error) {
    if (!StartSockets()) {
        error = "the network stack could not start";
        return kNoSocket;
    }
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &found) != 0 || found == nullptr) {
        error = "could not find " + host;
        return kNoSocket;
    }
    SocketHandle connected = kNoSocket;
    for (addrinfo* candidate = found; candidate != nullptr && connected == kNoSocket; candidate = candidate->ai_next) {
        SocketHandle attempt = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (attempt == kNoSocket) {
            continue;
        }
        if (connect(attempt, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) == 0) {
            connected = attempt;
        } else {
            CloseSocket(attempt);
        }
    }
    freeaddrinfo(found);
    if (connected == kNoSocket) {
        error = "could not reach " + host + ":" + port;
    }
    return connected;
}

bool SendAll(SocketHandle socket, const char* data, size_t size) {
    while (size > 0) {
        const int sent = static_cast<int>(send(socket, data, static_cast<int>(size), kSendFlags));
        if (sent <= 0) {
            return false;
        }
        data += sent;
        size -= static_cast<size_t>(sent);
    }
    return true;
}

int ReceiveSome(SocketHandle socket, char* buffer, size_t capacity) {
    return static_cast<int>(recv(socket, buffer, static_cast<int>(capacity), 0));
}

bool IsReadable(SocketHandle socket, int timeoutMs) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(socket, &readable);
    timeval timeout = { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    return select(static_cast<int>(socket) + 1, &readable, nullptr, nullptr, &timeout) > 0;
}

// Encrypts over the caller's TCP socket.
class TlsChannel {
  public:
    virtual ~TlsChannel() = default;
    virtual bool Handshake(const std::string& host, std::string& error) = 0;
    virtual bool Write(const char* data, size_t size) = 0;
    // Appends the plaintext of one transport read; false once the session is over.
    virtual bool Read(std::string& plain) = 0;
    virtual bool HasBuffered() const = 0;
};

#ifdef _WIN32

constexpr DWORD kContextFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                                ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_EXTENDED_ERROR;

class SchannelChannel final : public TlsChannel {
  public:
    explicit SchannelChannel(SocketHandle socket) : mSocket(socket) {
    }

    ~SchannelChannel() override {
        if (mHasContext) {
            DeleteSecurityContext(&mContext);
        }
        if (mHasCredentials) {
            FreeCredentialsHandle(&mCredentials);
        }
    }

    bool Handshake(const std::string& host, std::string& error) override {
        SCHANNEL_CRED credentials = {};
        credentials.dwVersion = SCHANNEL_CRED_VERSION;
        // TLS 1.3 under Schannel adds post-handshake renegotiation traffic; every Archipelago host speaks 1.2.
        credentials.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
        credentials.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
        if (AcquireCredentialsHandleA(nullptr, const_cast<LPSTR>(UNISP_NAME_A), SECPKG_CRED_OUTBOUND, nullptr,
                                      &credentials, nullptr, nullptr, &mCredentials, nullptr) != SEC_E_OK) {
            error = "Windows refused TLS credentials";
            return false;
        }
        mHasCredentials = true;
        SecBuffer token;
        SECURITY_STATUS status = Step(host, nullptr, token);
        if (status != SEC_I_CONTINUE_NEEDED || !SendToken(token)) {
            error = "the secure connection could not start";
            return false;
        }
        bool needsInput = true;
        while (true) {
            if (needsInput && !ReadCipher()) {
                error = "the server closed the secure connection";
                return false;
            }
            SecBuffer inputs[2] = { { static_cast<unsigned long>(mCipher.size()), SECBUFFER_TOKEN, mCipher.data() },
                                    { 0, SECBUFFER_EMPTY, nullptr } };
            SecBufferDesc input = { SECBUFFER_VERSION, 2, inputs };
            status = Step(host, &input, token);
            if (status == SEC_E_INCOMPLETE_MESSAGE) {
                needsInput = true;
                continue;
            }
            if (!SendToken(token) || FAILED(status)) {
                error = "the secure handshake failed";
                return false;
            }
            if (inputs[1].BufferType == SECBUFFER_EXTRA) {
                mCipher.erase(0, mCipher.size() - inputs[1].cbBuffer);
            } else {
                mCipher.clear();
            }
            if (status == SEC_E_OK) {
                break;
            }
            needsInput = mCipher.empty();
        }
        if (QueryContextAttributesA(&mContext, SECPKG_ATTR_STREAM_SIZES, &mSizes) != SEC_E_OK) {
            error = "the secure connection has no record sizes";
            return false;
        }
        return true;
    }

    bool Write(const char* data, size_t size) override {
        std::string record;
        while (size > 0) {
            const size_t chunk = std::min<size_t>(size, mSizes.cbMaximumMessage);
            record.resize(mSizes.cbHeader + chunk + mSizes.cbTrailer);
            char* body = record.data() + mSizes.cbHeader;
            std::memcpy(body, data, chunk);
            SecBuffer buffers[4] = { { mSizes.cbHeader, SECBUFFER_STREAM_HEADER, record.data() },
                                     { static_cast<unsigned long>(chunk), SECBUFFER_DATA, body },
                                     { mSizes.cbTrailer, SECBUFFER_STREAM_TRAILER, body + chunk },
                                     { 0, SECBUFFER_EMPTY, nullptr } };
            SecBufferDesc message = { SECBUFFER_VERSION, 4, buffers };
            if (EncryptMessage(&mContext, 0, &message, 0) != SEC_E_OK) {
                return false;
            }
            const size_t recordSize = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
            if (!SendAll(mSocket, record.data(), recordSize)) {
                return false;
            }
            data += chunk;
            size -= chunk;
        }
        return true;
    }

    bool Read(std::string& plain) override {
        const size_t before = plain.size();
        if (!Decrypt(plain)) {
            return false;
        }
        if (plain.size() > before) {
            return true;
        }
        return ReadCipher() && Decrypt(plain);
    }

    bool HasBuffered() const override {
        return !mCipher.empty();
    }

  private:
    SECURITY_STATUS Step(const std::string& host, SecBufferDesc* input, SecBuffer& token) {
        token = { 0, SECBUFFER_TOKEN, nullptr };
        SecBufferDesc output = { SECBUFFER_VERSION, 1, &token };
        DWORD flags = 0;
        const SECURITY_STATUS status = InitializeSecurityContextA(
            &mCredentials, mHasContext ? &mContext : nullptr, const_cast<SEC_CHAR*>(host.c_str()), kContextFlags, 0,
            0, input, 0, &mContext, &output, &flags, nullptr);
        mHasContext = true;
        return status;
    }

    bool SendToken(SecBuffer& token) {
        if (token.pvBuffer == nullptr) {
            return true;
        }
        const bool sent = token.cbBuffer == 0 || SendAll(mSocket, static_cast<const char*>(token.pvBuffer), token.cbBuffer);
        FreeContextBuffer(token.pvBuffer);
        token.pvBuffer = nullptr;
        return sent;
    }

    bool ReadCipher() {
        char buffer[kReadChunk];
        const int received = ReceiveSome(mSocket, buffer, sizeof(buffer));
        if (received <= 0) {
            return false;
        }
        mCipher.append(buffer, static_cast<size_t>(received));
        return true;
    }

    bool Decrypt(std::string& plain) {
        while (!mCipher.empty()) {
            SecBuffer buffers[4] = { { static_cast<unsigned long>(mCipher.size()), SECBUFFER_DATA, mCipher.data() },
                                     { 0, SECBUFFER_EMPTY, nullptr },
                                     { 0, SECBUFFER_EMPTY, nullptr },
                                     { 0, SECBUFFER_EMPTY, nullptr } };
            SecBufferDesc message = { SECBUFFER_VERSION, 4, buffers };
            const SECURITY_STATUS status = DecryptMessage(&mContext, &message, 0, nullptr);
            if (status == SEC_E_INCOMPLETE_MESSAGE) {
                return true;
            }
            if (status != SEC_E_OK) {
                return false;
            }
            // Decryption is in place, so the extra tail still points into mCipher.
            std::string extra;
            for (const SecBuffer& buffer : buffers) {
                if (buffer.BufferType == SECBUFFER_DATA) {
                    plain.append(static_cast<const char*>(buffer.pvBuffer), buffer.cbBuffer);
                } else if (buffer.BufferType == SECBUFFER_EXTRA) {
                    extra.assign(static_cast<const char*>(buffer.pvBuffer), buffer.cbBuffer);
                }
            }
            mCipher = std::move(extra);
        }
        return true;
    }

    SocketHandle mSocket;
    CredHandle mCredentials = {};
    CtxtHandle mContext = {};
    bool mHasCredentials = false;
    bool mHasContext = false;
    SecPkgContext_StreamSizes mSizes = {};
    std::string mCipher;
};

std::unique_ptr<TlsChannel> CreateTls(SocketHandle socket, std::string&) {
    return std::make_unique<SchannelChannel>(socket);
}

#else

// libssl is loaded at run time: every desktop Linux ships it, and Fleet then needs no OpenSSL to build.
struct OpenSsl {
    void* library = nullptr;
    int (*InitSsl)(uint64_t, const void*) = nullptr;
    const void* (*ClientMethod)() = nullptr;
    void* (*NewContext)(const void*) = nullptr;
    void (*FreeContext)(void*) = nullptr;
    int (*UseDefaultVerifyPaths)(void*) = nullptr;
    void (*SetVerify)(void*, int, void*) = nullptr;
    void* (*NewSession)(void*) = nullptr;
    void (*FreeSession)(void*) = nullptr;
    int (*SetDescriptor)(void*, int) = nullptr;
    long (*Control)(void*, int, long, void*) = nullptr;
    int (*SetExpectedHost)(void*, const char*) = nullptr;
    int (*ConnectSession)(void*) = nullptr;
    int (*ReadSession)(void*, void*, int) = nullptr;
    int (*WriteSession)(void*, const void*, int) = nullptr;
    int (*PendingBytes)(const void*) = nullptr;
    int (*Shutdown)(void*) = nullptr;
};

template <class Function> bool Bind(void* library, const char* name, Function& function) {
    function = reinterpret_cast<Function>(dlsym(library, name));
    return function != nullptr;
}

const OpenSsl* LoadOpenSsl() {
    static const OpenSsl loaded = [] {
        OpenSsl ssl;
        for (const char* name : { "libssl.so.3", "libssl.so.1.1", "libssl.so", "/opt/homebrew/opt/openssl@3/lib/libssl.3.dylib",
                                  "/usr/local/opt/openssl@3/lib/libssl.3.dylib" }) {
            ssl.library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
            if (ssl.library != nullptr) {
                break;
            }
        }
        if (ssl.library == nullptr) {
            return ssl;
        }
        const bool bound =
            Bind(ssl.library, "OPENSSL_init_ssl", ssl.InitSsl) && Bind(ssl.library, "TLS_client_method", ssl.ClientMethod) &&
            Bind(ssl.library, "SSL_CTX_new", ssl.NewContext) && Bind(ssl.library, "SSL_CTX_free", ssl.FreeContext) &&
            Bind(ssl.library, "SSL_CTX_set_default_verify_paths", ssl.UseDefaultVerifyPaths) &&
            Bind(ssl.library, "SSL_CTX_set_verify", ssl.SetVerify) && Bind(ssl.library, "SSL_new", ssl.NewSession) &&
            Bind(ssl.library, "SSL_free", ssl.FreeSession) && Bind(ssl.library, "SSL_set_fd", ssl.SetDescriptor) &&
            Bind(ssl.library, "SSL_ctrl", ssl.Control) && Bind(ssl.library, "SSL_set1_host", ssl.SetExpectedHost) &&
            Bind(ssl.library, "SSL_connect", ssl.ConnectSession) && Bind(ssl.library, "SSL_read", ssl.ReadSession) &&
            Bind(ssl.library, "SSL_write", ssl.WriteSession) && Bind(ssl.library, "SSL_pending", ssl.PendingBytes) &&
            Bind(ssl.library, "SSL_shutdown", ssl.Shutdown);
        if (!bound || ssl.InitSsl(0, nullptr) != 1) {
            dlclose(ssl.library);
            ssl.library = nullptr;
        }
        return ssl;
    }();
    return loaded.library != nullptr ? &loaded : nullptr;
}

constexpr int kVerifyPeer = 1;
constexpr int kSetServerNameControl = 55;
constexpr long kHostNameType = 0;

class OpenSslChannel final : public TlsChannel {
  public:
    OpenSslChannel(const OpenSsl& ssl, SocketHandle socket) : mSsl(ssl), mSocket(socket) {
    }

    ~OpenSslChannel() override {
        if (mSession != nullptr) {
            mSsl.Shutdown(mSession);
            mSsl.FreeSession(mSession);
        }
        if (mContext != nullptr) {
            mSsl.FreeContext(mContext);
        }
    }

    bool Handshake(const std::string& host, std::string& error) override {
        mContext = mSsl.NewContext(mSsl.ClientMethod());
        if (mContext == nullptr) {
            error = "OpenSSL could not create a context";
            return false;
        }
        mSsl.UseDefaultVerifyPaths(mContext);
        mSsl.SetVerify(mContext, kVerifyPeer, nullptr);
        mSession = mSsl.NewSession(mContext);
        if (mSession == nullptr || mSsl.SetDescriptor(mSession, mSocket) != 1) {
            error = "OpenSSL could not open a session";
            return false;
        }
        mSsl.Control(mSession, kSetServerNameControl, kHostNameType, const_cast<char*>(host.c_str()));
        mSsl.SetExpectedHost(mSession, host.c_str());
        if (mSsl.ConnectSession(mSession) != 1) {
            error = "the secure handshake with " + host + " failed";
            return false;
        }
        return true;
    }

    bool Write(const char* data, size_t size) override {
        while (size > 0) {
            const int written = mSsl.WriteSession(mSession, data, static_cast<int>(size));
            if (written <= 0) {
                return false;
            }
            data += written;
            size -= static_cast<size_t>(written);
        }
        return true;
    }

    bool Read(std::string& plain) override {
        char buffer[kReadChunk];
        const int received = mSsl.ReadSession(mSession, buffer, sizeof(buffer));
        if (received <= 0) {
            return false;
        }
        plain.append(buffer, static_cast<size_t>(received));
        return true;
    }

    bool HasBuffered() const override {
        return mSsl.PendingBytes(mSession) > 0;
    }

  private:
    const OpenSsl& mSsl;
    SocketHandle mSocket;
    void* mContext = nullptr;
    void* mSession = nullptr;
};

std::unique_ptr<TlsChannel> CreateTls(SocketHandle socket, std::string& error) {
    const OpenSsl* ssl = LoadOpenSsl();
    if (ssl == nullptr) {
        error = "wss:// needs OpenSSL (libssl), which this system does not have";
        return nullptr;
    }
    return std::make_unique<OpenSslChannel>(*ssl, socket);
}

#endif

std::string EncodeBase64(const uint8_t* data, size_t size) {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t chunk = (uint32_t{ data[i] } << 16) | (i + 1 < size ? uint32_t{ data[i + 1] } << 8 : 0) |
                               (i + 2 < size ? uint32_t{ data[i + 2] } : 0);
        encoded += kAlphabet[(chunk >> 18) & 63];
        encoded += kAlphabet[(chunk >> 12) & 63];
        encoded += i + 1 < size ? kAlphabet[(chunk >> 6) & 63] : '=';
        encoded += i + 2 < size ? kAlphabet[chunk & 63] : '=';
    }
    return encoded;
}

enum Opcode : uint8_t {
    kContinuation = 0x0,
    kText = 0x1,
    kBinary = 0x2,
    kClose = 0x8,
    kPing = 0x9,
    kPong = 0xA,
};

} // namespace

struct ApSocket::Link {
    SocketHandle socket = kNoSocket;
    std::unique_ptr<TlsChannel> tls;
    std::string received;
    std::string message;
    uint8_t messageOpcode = kText;
    std::mt19937 random{ std::random_device{}() };

    ~Link() {
        tls.reset();
        if (socket != kNoSocket) {
            CloseSocket(socket);
        }
    }

    bool Write(const std::string& bytes) {
        return tls ? tls->Write(bytes.data(), bytes.size()) : SendAll(socket, bytes.data(), bytes.size());
    }

    bool ReadMore() {
        if (tls) {
            return tls->Read(received);
        }
        char buffer[kReadChunk];
        const int count = ReceiveSome(socket, buffer, sizeof(buffer));
        if (count <= 0) {
            return false;
        }
        received.append(buffer, static_cast<size_t>(count));
        return true;
    }

    bool HasBuffered() const {
        return tls && tls->HasBuffered();
    }

    // Client frames are always masked (RFC 6455 section 5.3).
    bool SendFrame(uint8_t opcode, const std::string& payload) {
        std::string frame;
        frame += static_cast<char>(0x80 | opcode);
        const size_t size = payload.size();
        if (size < 126) {
            frame += static_cast<char>(0x80 | size);
        } else if (size <= 0xFFFF) {
            frame += static_cast<char>(0x80 | 126);
            frame += static_cast<char>((size >> 8) & 0xFF);
            frame += static_cast<char>(size & 0xFF);
        } else {
            frame += static_cast<char>(0x80 | 127);
            for (int shift = 56; shift >= 0; shift -= 8) {
                frame += static_cast<char>((static_cast<uint64_t>(size) >> shift) & 0xFF);
            }
        }
        std::array<uint8_t, 4> mask;
        for (uint8_t& byte : mask) {
            byte = static_cast<uint8_t>(random() & 0xFF);
        }
        frame.append(reinterpret_cast<const char*>(mask.data()), mask.size());
        for (size_t i = 0; i < size; i++) {
            frame += static_cast<char>(payload[i] ^ mask[i % 4]);
        }
        return Write(frame);
    }

    bool Upgrade(const std::string& host, const std::string& port, std::string& error) {
        std::array<uint8_t, 16> nonce;
        for (uint8_t& byte : nonce) {
            byte = static_cast<uint8_t>(random() & 0xFF);
        }
        const std::string request = "GET / HTTP/1.1\r\nHost: " + host + ":" + port +
                                    "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " +
                                    EncodeBase64(nonce.data(), nonce.size()) + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
        if (!Write(request)) {
            error = "the server closed the connection";
            return false;
        }
        size_t headerEnd = std::string::npos;
        while ((headerEnd = received.find("\r\n\r\n")) == std::string::npos) {
            if (!ReadMore()) {
                error = "the server did not answer as an Archipelago server";
                return false;
            }
        }
        const std::string status = received.substr(0, received.find("\r\n"));
        if (status.find(" 101") == std::string::npos) {
            error = "the server refused the connection (" + status + ")";
            return false;
        }
        received.erase(0, headerEnd + 4);
        return true;
    }

    // Consumes whole frames from `received`; false on a close frame or a protocol error.
    bool TakeFrames(std::vector<std::string>& messages) {
        while (received.size() >= 2) {
            const uint8_t first = static_cast<uint8_t>(received[0]);
            const uint8_t second = static_cast<uint8_t>(received[1]);
            const bool isFinal = (first & 0x80) != 0;
            const uint8_t opcode = first & 0x0F;
            const bool isMasked = (second & 0x80) != 0;
            uint64_t size = second & 0x7F;
            size_t offset = 2;
            const size_t extension = size == 126 ? 2 : size == 127 ? 8 : 0;
            if (received.size() < offset + extension) {
                return true;
            }
            if (extension > 0) {
                size = 0;
                for (size_t i = 0; i < extension; i++) {
                    size = (size << 8) | static_cast<uint8_t>(received[offset + i]);
                }
                offset += extension;
            }
            const size_t maskSize = isMasked ? 4 : 0;
            if (received.size() < offset + maskSize + size) {
                return true;
            }
            std::string payload = received.substr(offset + maskSize, static_cast<size_t>(size));
            if (isMasked) {
                for (size_t i = 0; i < payload.size(); i++) {
                    payload[i] = static_cast<char>(payload[i] ^ received[offset + i % 4]);
                }
            }
            received.erase(0, offset + maskSize + static_cast<size_t>(size));
            switch (opcode) {
                case kText:
                case kBinary:
                    messageOpcode = opcode;
                    message = std::move(payload);
                    break;
                case kContinuation:
                    message += payload;
                    break;
                case kPing:
                    if (!SendFrame(kPong, payload)) {
                        return false;
                    }
                    continue;
                case kPong:
                    continue;
                case kClose:
                    SendFrame(kClose, {});
                    return false;
                default:
                    return false;
            }
            if (isFinal) {
                if (messageOpcode == kText) {
                    messages.push_back(std::move(message));
                }
                message.clear();
            }
        }
        return true;
    }
};

ApSocket::ApSocket() = default;

ApSocket::~ApSocket() {
    Close();
}

bool ApSocket::Open(const std::string& address, std::string& error) {
    std::string scheme;
    std::string rest = address;
    if (const size_t separator = rest.find("://"); separator != std::string::npos) {
        scheme = rest.substr(0, separator);
        rest.erase(0, separator + 3);
    }
    while (!rest.empty() && rest.back() == '/') {
        rest.pop_back();
    }
    std::string host = rest;
    std::string port = kDefaultPort;
    if (const size_t colon = rest.rfind(':'); colon != std::string::npos) {
        host = rest.substr(0, colon);
        port = rest.substr(colon + 1);
    }
    if (host.empty()) {
        error = "no server address";
        return false;
    }
    if (!scheme.empty()) {
        return OpenUrl(scheme, host, port, error);
    }
    std::string secureError;
    if (OpenUrl("wss", host, port, secureError)) {
        return true;
    }
    if (OpenUrl("ws", host, port, error)) {
        return true;
    }
    error = secureError;
    return false;
}

bool ApSocket::OpenUrl(const std::string& scheme, const std::string& host, const std::string& port,
                       std::string& error) {
    Close();
    if (scheme != "ws" && scheme != "wss") {
        error = "unknown address scheme " + scheme;
        return false;
    }
    auto link = std::make_unique<Link>();
    link->socket = ConnectTcp(host, port, error);
    if (link->socket == kNoSocket) {
        return false;
    }
    if (scheme == "wss") {
        link->tls = CreateTls(link->socket, error);
        if (!link->tls || !link->tls->Handshake(host, error)) {
            return false;
        }
    }
    if (!link->Upgrade(host, port, error)) {
        return false;
    }
    mLink = std::move(link);
    return true;
}

bool ApSocket::SendText(const std::string& text) {
    return mLink != nullptr && mLink->SendFrame(kText, text);
}

bool ApSocket::Poll(int timeoutMs, std::vector<std::string>& messages) {
    if (mLink == nullptr) {
        return false;
    }
    if (!mLink->TakeFrames(messages)) {
        Close();
        return false;
    }
    if (!mLink->HasBuffered() && !IsReadable(mLink->socket, timeoutMs)) {
        return true;
    }
    if (!mLink->ReadMore() || !mLink->TakeFrames(messages)) {
        Close();
        return false;
    }
    return true;
}

void ApSocket::Close() {
    mLink.reset();
}
