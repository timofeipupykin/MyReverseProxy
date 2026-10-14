#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <cstdint>
#include <latch>

#include "proxy/load_balancer.hpp"
#include "proxy/routing_table.hpp"

namespace proxy {

struct ServerConfig {
    std::uint16_t port = 8080;
    std::chrono::milliseconds connect_timeout{3000};
};

class ProxyServer {
public:
    // Бросает boost::system::system_error, если порт занят.
    ProxyServer(boost::asio::io_context& ctx, ServerConfig cfg, RoutingTable& table,
                const Balancer& balancer);
    ~ProxyServer();

    ProxyServer(const ProxyServer&) = delete;
    ProxyServer& operator=(const ProxyServer&) = delete;

    void start();  // запускает accept-корутину
    void stop();   // закрывает listener и ждёт завершения accept-корутины

private:
    boost::asio::awaitable<void> listen();

    boost::asio::io_context& ctx_;
    ServerConfig cfg_;
    RoutingTable& table_;
    const Balancer& balancer_;

    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::latch listener_done_{1};
    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};
};

}  // namespace proxy
