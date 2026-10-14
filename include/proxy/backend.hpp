#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace proxy {

struct Backend {
    Backend(std::string h, std::uint16_t p) : host(std::move(h)), port(p) {}
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    const std::string host;
    const std::uint16_t port;
    std::atomic<bool> healthy{true};
    std::atomic<std::size_t> active_connections{0};

    [[nodiscard]] std::string address() const { return host + ":" + std::to_string(port); }
};

using BackendPtr = std::shared_ptr<Backend>;

// RAII-счётчик активных соединений (нужен для Least Connections).
class ConnectionGuard {
public:
    explicit ConnectionGuard(BackendPtr b) : backend_(std::move(b)) {
        backend_->active_connections.fetch_add(1, std::memory_order_relaxed);
    }
    ~ConnectionGuard() {
        if (backend_) backend_->active_connections.fetch_sub(1, std::memory_order_relaxed);
    }
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

private:
    BackendPtr backend_;
};

}  // namespace proxy
