#include "proxy/proxy_server.hpp"

#include <array>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <exception>
#include <expected>
#include <format>
#include <string>

#include "proxy/http_parser.hpp"
#include "proxy/logger.hpp"
#include "proxy/net.hpp"

namespace proxy {

namespace asio = boost::asio;
using asio::ip::tcp;
using boost::system::error_code;

namespace {

std::string peer_label(const tcp::socket& s) {
    error_code ec;
    const auto ep = s.remote_endpoint(ec);
    return ec ? "?" : std::format("{}:{}", ep.address().to_string(), ep.port());
}

struct HttpStatus {
    int code;
    std::string_view text;
};

HttpStatus status_for(Error e) {
    switch (e) {
        case Error::HeaderTooLarge:       return {431, "Request Header Fields Too Large"};
        case Error::NoHealthyBackend:     return {503, "Service Unavailable"};
        case Error::BackendConnectFailed: return {502, "Bad Gateway"};
        case Error::BackendTimeout:       return {504, "Gateway Timeout"};
        default:                          return {400, "Bad Request"};
    }
}

asio::awaitable<void> reply_error(tcp::socket& client, Error e) {
    const auto [code, text] = status_for(e);
    const std::string body = std::format("{} {}: {}\n", code, text, to_string(e));
    const std::string response = std::format(
        "HTTP/1.1 {} {}\r\nContent-Type: text/plain\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
        code, text, body.size(), body);
    co_await asio::async_write(client, asio::buffer(response), asio::use_awaitable);
    error_code ec;
    client.shutdown(tcp::socket::shutdown_send, ec);
}

// Односторонняя перекачка байтов from -> to.
asio::awaitable<void> pipe(tcp::socket& from, tcp::socket& to) {
    std::array<char, 16 * 1024> buf;
    error_code ec;
    try {
        for (;;) {
            const auto n = co_await from.async_read_some(asio::buffer(buf), asio::use_awaitable);
            co_await asio::async_write(to, asio::buffer(buf.data(), n), asio::use_awaitable);
        }
    } catch (const boost::system::system_error& e) {
        if (e.code() == asio::error::eof) {
            to.shutdown(tcp::socket::shutdown_send, ec);  // пробрасываем half-close
        } else {
            from.close(ec);  // ошибка - рвём обе стороны, чтобы второй pipe тоже завершился
            to.close(ec);
        }
    }
}

asio::awaitable<void> session(tcp::socket client, RoutingTable& table, const Balancer& balancer,
                              std::chrono::milliseconds connect_timeout) {
    using namespace asio::experimental::awaitable_operators;
    const std::string peer = peer_label(client);

    try {
        // 1. Читаем заголовки запроса.
        std::string data;
        std::array<char, 4096> chunk{};
        std::expected<HttpRequestHead, Error> head = std::unexpected(Error::IncompleteRequest);
        for (;;) {
            head = parse_request_head(std::string_view{data});
            if (head || head.error() != Error::IncompleteRequest) break;
            const auto n = co_await client.async_read_some(asio::buffer(chunk), asio::use_awaitable);
            data.append(chunk.data(), n);
        }
        if (!head) {
            co_await reply_error(client, head.error());
            co_return;
        }

        // 2. Выбираем бэкенд (shared_lock внутри таблицы).
        auto picked = table.select(balancer);
        if (!picked) {
            log("{} {} {} -> {}", peer, head->method, head->target, to_string(picked.error()));
            co_await reply_error(client, picked.error());
            co_return;
        }
        BackendPtr backend = *picked;
        ConnectionGuard guard(backend);

        // 3. Подключаемся к бэкенду.
        auto upstream = co_await connect_with_timeout(backend->host, backend->port, connect_timeout);
        if (!upstream) {
            backend->healthy.store(false);  // пассивная проверка; health checker вернёт UP
            log("{} {} {} -> backend {} failed: {}", peer, head->method, head->target,
                backend->address(), to_string(upstream.error()));
            co_await reply_error(client, upstream.error());
            co_return;
        }
        log("{} {} {} -> {}", peer, head->method, head->target, backend->address());

        // 4. Пересылаем уже прочитанные байты и дальше прозрачно гоним трафик в обе стороны.
        co_await asio::async_write(*upstream, asio::buffer(data), asio::use_awaitable);
        co_await (pipe(client, *upstream) && pipe(*upstream, client));
    } catch (const boost::system::system_error& e) {
        const auto c = e.code();
        if (c != asio::error::eof && c != asio::error::connection_reset &&
            c != asio::error::operation_aborted && c != asio::error::broken_pipe) {
            log("{} session error: {}", peer, e.what());
        }
    } catch (const std::exception& e) {
        log("{} session exception: {}", peer, e.what());
    }
}

}  // namespace

ProxyServer::ProxyServer(asio::io_context& ctx, ServerConfig cfg, RoutingTable& table,
                         const Balancer& balancer)
    : ctx_(ctx),
      cfg_(cfg),
      table_(table),
      balancer_(balancer),
      strand_(asio::make_strand(ctx)),
      acceptor_(strand_) {
    const tcp::endpoint ep(tcp::v4(), cfg_.port);
    acceptor_.open(ep.protocol());
#ifdef _WIN32
    // На Windows SO_REUSEADDR позволяет второму процессу занять тот же порт;
    // SO_EXCLUSIVEADDRUSE гарантирует ошибку "порт занят", как на POSIX.
    using exclusive_address_use = asio::detail::socket_option::boolean<SOL_SOCKET, SO_EXCLUSIVEADDRUSE>;
    acceptor_.set_option(exclusive_address_use(true));
#else
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
#endif
    acceptor_.bind(ep);
    acceptor_.listen(asio::socket_base::max_listen_connections);
}

ProxyServer::~ProxyServer() { stop(); }

void ProxyServer::start() {
    if (started_.exchange(true)) return;
    asio::co_spawn(strand_, listen(), asio::detached);
}

void ProxyServer::stop() {
    if (stopped_.exchange(true)) return;
    if (!started_.load()) return;
    asio::post(strand_, [this] {
        error_code ec;
        acceptor_.close(ec);
    });
    listener_done_.wait();
}

asio::awaitable<void> ProxyServer::listen() {
    struct Done {
        std::latch& latch;
        ~Done() { latch.count_down(); }
    } done{listener_done_};

    log("listening on 0.0.0.0:{}", cfg_.port);

    for (;;) {
        bool transient_error = false;
        try {
            // Каждому клиенту - свой strand: корутины одного соединения не гоняются друг с другом,
            // а разные соединения исполняются параллельно на всём пуле.
            tcp::socket client(asio::make_strand(ctx_));
            co_await acceptor_.async_accept(client, asio::use_awaitable);
            client.set_option(tcp::no_delay(true));

            auto ex = client.get_executor();
            asio::co_spawn(ex, session(std::move(client), table_, balancer_, cfg_.connect_timeout),
                           asio::detached);
        } catch (const boost::system::system_error& e) {
            if (e.code() == asio::error::operation_aborted || e.code() == asio::error::bad_descriptor) {
                break;  // listener закрыт
            }
            log("accept error: {}", e.what());
            transient_error = true;
        }

        if (transient_error) {  // например, EMFILE - не крутимся в busy-loop
            asio::steady_timer t(co_await asio::this_coro::executor, std::chrono::milliseconds(100));
            co_await t.async_wait(asio::use_awaitable);
        }
    }
    log("listener stopped");
}

}  // namespace proxy
