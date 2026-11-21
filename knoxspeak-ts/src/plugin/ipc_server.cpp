#include "core/protocol.hpp"

#include "plugin/ipc_server.hpp"

#include <array>
#include <format>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace knoxspeak {
namespace {

constexpr auto kInvalid = static_cast<std::uintptr_t>(INVALID_SOCKET);

SOCKET sock(std::uintptr_t s) {
    return static_cast<SOCKET>(s);
}

void closeSocket(std::uintptr_t& s) {
    if (s != kInvalid) {
        ::closesocket(sock(s));
        s = kInvalid;
    }
}

} // namespace

IpcServer::IpcServer(std::uint16_t port, Callbacks callbacks)
    : port_(port), cb_(std::move(callbacks)), client_(kInvalid) {}

IpcServer::~IpcServer() {
    stop();
}

void IpcServer::start() {
    if (!thread_.joinable()) {
        thread_ = std::jthread([this](std::stop_token st) { run(st); });
    }
}

void IpcServer::stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

void IpcServer::send(std::string_view line) {
    std::scoped_lock lock(sendMutex_);
    if (client_ == kInvalid) {
        return;
    }
    while (!line.empty()) {
        const int n = ::send(sock(client_), line.data(), static_cast<int>(line.size()), 0);
        if (n <= 0) {
            // read loop will catch this
            return;
        }
        line.remove_prefix(static_cast<std::size_t>(n));
    }
}

void IpcServer::run(std::stop_token stop) {
    const auto& log = cb_.onLog;

    WSADATA wsa{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        log("WSAStartup failed");
        return;
    }

    std::uintptr_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalid) {
        log(std::format("socket() failed: {}", ::WSAGetLastError()));
        ::WSACleanup();
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(port_);
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK); // loopback only, never exposed to the network

    BOOL exclusive = TRUE;
    ::setsockopt(sock(listener), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                 sizeof(exclusive));

    if (::bind(sock(listener), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        ::listen(sock(listener), 1) == SOCKET_ERROR) {
        log(std::format("bind/listen on 127.0.0.1:{} failed: {}", port_, ::WSAGetLastError()));
        closeSocket(listener);
        ::WSACleanup();
        return;
    }
    log(std::format("listening on 127.0.0.1:{}", port_));

    proto::LineSplitter splitter;
    std::array<char, 4096> buf{};

    auto dropClient = [&] {
        bool had = false;
        {
            std::scoped_lock lock(sendMutex_);
            had = client_ != kInvalid;
            closeSocket(client_);
        }
        splitter.reset();
        if (had) {
            cb_.onDisconnected();
        }
    };

    while (!stop.stop_requested()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock(listener), &readSet);
        std::uintptr_t client;
        {
            std::scoped_lock lock(sendMutex_);
            client = client_;
        }
        if (client != kInvalid) {
            FD_SET(sock(client), &readSet);
        }

        timeval tv{0, 200'000}; // wake regularly to observe stop requests
        const int ready = ::select(0, &readSet, nullptr, nullptr, &tv);
        if (ready == SOCKET_ERROR) {
            log(std::format("select failed: {}", ::WSAGetLastError()));
            break;
        }
        if (ready == 0) {
            continue;
        }

        if (FD_ISSET(sock(listener), &readSet)) {
            std::uintptr_t incoming = ::accept(sock(listener), nullptr, nullptr);
            if (incoming != kInvalid) {
                dropClient(); // newest connection wins
                BOOL noDelay = TRUE;
                ::setsockopt(sock(incoming), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay),
                             sizeof(noDelay));
                {
                    std::scoped_lock lock(sendMutex_);
                    client_ = incoming;
                    client = incoming;
                }
                cb_.onConnected();
            }
        }

        if (client != kInvalid && FD_ISSET(sock(client), &readSet)) {
            const int n = ::recv(sock(client), buf.data(), static_cast<int>(buf.size()), 0);
            if (n <= 0) {
                dropClient();
                continue;
            }
            if (!splitter.append(std::string_view(buf.data(), static_cast<std::size_t>(n)), cb_.onLine)) {
                log("line too long; dropping game connection");
                dropClient();
            }
        }
    }

    dropClient();
    closeSocket(listener);
    ::WSACleanup();
}

} // namespace knoxspeak
