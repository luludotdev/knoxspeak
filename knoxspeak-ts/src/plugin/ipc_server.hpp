#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string_view>
#include <thread>

namespace knoxspeak {

class IpcServer {
  public:
    struct Callbacks {
        std::function<void()> onConnected;
        std::function<void(std::string_view line)> onLine;
        std::function<void()> onDisconnected;
        std::function<void(std::string_view message)> onLog;
    };

    IpcServer(std::uint16_t port, Callbacks callbacks);
    ~IpcServer();
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    void start();
    void stop();

    // thread safe
    // drops silently if no game is connected
    void send(std::string_view line);

  private:
    void run(std::stop_token stop);

    std::uint16_t port_;
    Callbacks cb_;
    std::jthread thread_;
    std::mutex sendMutex_;
    std::uintptr_t client_; // SOCKET, guarded by sendMutex_ for writes
};

} // namespace knoxspeak
