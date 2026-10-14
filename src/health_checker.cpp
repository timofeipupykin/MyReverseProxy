#include "proxy/health_checker.hpp"

#include <boost/asio.hpp>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <vector>

#include "proxy/logger.hpp"
#include "proxy/net.hpp"

namespace proxy {

namespace asio = boost::asio;

namespace {

asio::awaitable<bool> probe(std::string host, std::uint16_t port, std::chrono::milliseconds timeout) {
    auto socket = co_await connect_with_timeout(std::move(host), port, timeout);
    co_return socket.has_value();  // сокет закроется в деструкторе
}

}  // namespace

HealthChecker::HealthChecker(RoutingTable& table, std::chrono::milliseconds interval,
                             std::chrono::milliseconds timeout)
    : table_(table), interval_(interval), timeout_(timeout) {}

HealthChecker::~HealthChecker() = default;  // ~jthread: request_stop() + join()

void HealthChecker::start() {
    worker_ = std::jthread([this](std::stop_token st) { run(std::move(st)); });
}

void HealthChecker::run(std::stop_token st) {
    asio::io_context ctx(1);  // собственный однопоточный контекст
    std::mutex mtx;
    std::condition_variable_any cv;

    while (!st.stop_requested()) {
        const auto backends = table_.snapshot();
        std::vector<char> alive(backends.size(), 0);

        // Проверяем все бэкенды параллельно.
        for (std::size_t i = 0; i < backends.size(); ++i) {
            asio::co_spawn(ctx, probe(backends[i]->host, backends[i]->port, timeout_),
                           [&alive, i](std::exception_ptr, bool ok) { alive[i] = ok ? 1 : 0; });
        }
        ctx.run();
        ctx.restart();

        for (std::size_t i = 0; i < backends.size(); ++i) {
            const bool now = alive[i] != 0;
            const bool was = backends[i]->healthy.exchange(now);
            if (was != now) {
                log("health: backend {} is now {}", backends[i]->address(), now ? "UP" : "DOWN");
            }
        }

        // Спим interval_, но просыпаемся сразу по запросу остановки.
        std::unique_lock lock(mtx);
        cv.wait_for(lock, st, interval_, [] { return false; });
    }
}

}  // namespace proxy
