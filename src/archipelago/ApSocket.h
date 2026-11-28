#pragma once

#include <memory>
#include <string>
#include <vector>

// A WebSocket client (RFC 6455) for one Archipelago server. Every call must come from the same thread.
class ApSocket {
  public:
    ApSocket();
    ~ApSocket();
    ApSocket(const ApSocket&) = delete;
    ApSocket& operator=(const ApSocket&) = delete;

    // Accepts "host", "host:port", "ws://host:port" or "wss://host:port"; a bare address tries wss, then ws.
    bool Open(const std::string& address, std::string& error);
    bool SendText(const std::string& text);
    // Waits up to timeoutMs for traffic and appends every complete text message; false once the link is gone.
    bool Poll(int timeoutMs, std::vector<std::string>& messages);
    void Close();

  private:
    struct Link;
    bool OpenUrl(const std::string& scheme, const std::string& host, const std::string& port, std::string& error);
    std::unique_ptr<Link> mLink;
};
