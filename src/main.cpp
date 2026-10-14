#include <algorithm>
#include <atomic>
#include <boost/asio.hpp>
#include <charconv>
#include <csignal>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

#include "proxy/config.hpp"
#include "proxy/health_checker.hpp"
#include "proxy/load_balancer.hpp"
#include "proxy/logger.hpp"
#include "proxy/proxy_server.hpp"
#include "proxy/routing_table.hpp"
#include "proxy/thread_pool.hpp"

namespace asio = boost::asio;
using namespace std::chrono_literals;

namespace {

struct Options {
    std::uint16_t port = 8080;
    std::size_t threads = std::max(2u, std::thread::hardware_concurrency());
    std::filesystem::path config = "config/backends.conf";
    proxy::Algorithm algo = proxy::Algorithm::RoundRobin;
};

constexpr std::string_view kUsage =
    "usage: async_proxy [--port N] [--threads N] [--config FILE] [--algo rr|lc]\n"
    "  --port     порт для входящих соединений (по умолчанию 8080)\n"
    "  --threads  число рабочих потоков\n"
    "  --config   файл со списком бэкендов (host:port на строку)\n"
    "  --algo     rr = Round-Robin, lc = Least Connections\n"
    "  SIGHUP     перечитать конфиг без остановки сервера";

template <typename T>
bool parse_number(std::string_view s, T& out) {
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

std::expected<Options, std::string> parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto next = [&]() -> std::string_view { return i + 1 < argc ? argv[++i] : std::string_view{}; };

        if (arg == "--port") {
            if (!parse_number(next(), o.port)) return std::unexpected("bad --port");
        } else if (arg == "--threads") {
            if (!parse_number(next(), o.threads) || o.threads == 0) return std::unexpected("bad --threads");
        } else if (arg == "--config") {
            const auto v = next();
            if (v.empty()) return std::unexpected("bad --config");
            o.config = std::string(v);
        } else if (arg == "--algo") {
            const auto v = next();
            if (v == "rr") o.algo = proxy::Algorithm::RoundRobin;
            else if (v == "lc") o.algo = proxy::Algorithm::LeastConnections;
            else return std::unexpected("bad --algo (rr|lc)");
        } else {
            return std::unexpected(std::string("unknown argument: ") + std::string(arg));
        }
    }
    return o;
}

// SIGINT/SIGTERM -> graceful shutdown, SIGHUP -> горячая перезагрузка списка бэкендов.
asio::awaitable<void> signal_loop(proxy::RoutingTable& table, std::filesystem::path config,
                                  std::atomic_flag& quit) {
    asio::signal_set signals(co_await asio::this_coro::executor, SIGINT, SIGTERM);
#ifdef SIGHUP
    signals.add(SIGHUP);
#endif
    for (;;) {
        const int sig = co_await signals.async_wait(asio::use_awaitable);
#ifdef SIGHUP
        if (sig == SIGHUP) {
            if (auto fresh = proxy::load_backends(config)) {
                table.update(std::move(*fresh));
                proxy::log("config reloaded: {} backend(s)", table.size());
            } else {
                proxy::log("config reload failed: {} (keeping old config)", proxy::to_string(fresh.error()));
            }
            continue;
        }
#endif
        proxy::log("signal {} received, shutting down", sig);
        quit.test_and_set();
        quit.notify_all();
        co_return;
    }
}

}  // namespace

int main(int argc, char** argv) {
    const auto opts = parse_args(argc, argv);
    if (!opts) {
        proxy::log("{}\n{}", opts.error(), kUsage);
        return 2;
    }

    auto backends = proxy::load_backends(opts->config);
    if (!backends) {
        proxy::log("cannot load '{}': {}", opts->config.string(), proxy::to_string(backends.error()));
        return 1;
    }

    // Порядок объявления = обратный порядок уничтожения:
    // health -> server -> pool -> balancer -> table.
    proxy::RoutingTable table;
    table.update(std::move(*backends));
    const proxy::Balancer balancer(opts->algo);
    std::atomic_flag quit;

    try {
        proxy::IoThreadPool pool(opts->threads);
        proxy::ProxyServer server(pool.context(), proxy::ServerConfig{.port = opts->port}, table, balancer);
        proxy::HealthChecker health(table, 2s, 1s);

        proxy::log("async_proxy: {} threads, {} backend(s), algo={}", pool.size(), table.size(),
                   opts->algo == proxy::Algorithm::RoundRobin ? "round-robin" : "least-connections");

        asio::co_spawn(pool.context(), signal_loop(table, opts->config, quit), asio::detached);
        health.start();
        server.start();

        quit.wait(false);  // главный поток спит до сигнала

        health.stop();
        server.stop();
        pool.stop();
    } catch (const std::exception& e) {
        proxy::log("fatal: {}", e.what());
        return 1;
    }

    proxy::log("bye");
    return 0;
}
