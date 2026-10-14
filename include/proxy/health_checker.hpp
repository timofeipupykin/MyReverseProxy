#pragma once

#include <chrono>
#include <stop_token>
#include <thread>

#include "proxy/routing_table.hpp"

namespace proxy {

// Фоновая проверка здоровья бэкендов. Живёт в std::jthread и останавливается
// кооперативно через std::stop_token (никаких атомарных флагов "running").
class HealthChecker {
public:
    HealthChecker(RoutingTable& table, std::chrono::milliseconds interval,
                  std::chrono::milliseconds timeout);
    ~HealthChecker();

    HealthChecker(const HealthChecker&) = delete;
    HealthChecker& operator=(const HealthChecker&) = delete;

    void start();
    void stop() noexcept { worker_.request_stop(); }

private:
    void run(std::stop_token st);

    RoutingTable& table_;
    std::chrono::milliseconds interval_;
    std::chrono::milliseconds timeout_;
    std::jthread worker_;
};

}  // namespace proxy
