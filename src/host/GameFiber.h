#pragma once

#include <functional>
#include <memory>

class GameFiber {
  public:
    explicit GameFiber(std::function<void()> body);
    ~GameFiber();
    GameFiber(const GameFiber&) = delete;
    GameFiber& operator=(const GameFiber&) = delete;

    // Assumes the calling thread is the main thread and was prepared with EnterMainThread.
    void SwitchTo();
    static void SwitchToMain();
    static void EnterMainThread();

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
