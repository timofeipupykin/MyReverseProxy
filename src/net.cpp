#include "proxy/net.hpp"

#include <boost/asio/experimental/awaitable_operators.hpp>
#include <utility>

namespace proxy {

namespace asio = boost::asio;
using asio::ip::tcp;

asio::awaitable<std::expected<tcp::socket, Error>> connect_with_timeout(
    std::string host, std::uint16_t port, std::chrono::milliseconds timeout) {
    using namespace asio::experimental::awaitable_operators;

    const auto ex = co_await asio::this_coro::executor;
    tcp::resolver resolver(ex);
    tcp::socket socket(ex);
    asio::steady_timer timer(ex, timeout);

    try {
        auto endpoints = co_await resolver.async_resolve(host, std::to_string(port), asio::use_awaitable);
        // Кто завершился первым - тот и отменяет второго.
        auto result = co_await (asio::async_connect(socket, endpoints, asio::use_awaitable) ||
                                timer.async_wait(asio::use_awaitable));
        if (result.index() == 1) co_return std::unexpected(Error::BackendTimeout);
    } catch (const boost::system::system_error&) {
        co_return std::unexpected(Error::BackendConnectFailed);
    }

    socket.set_option(tcp::no_delay(true));
    co_return std::move(socket);
}

}  // namespace proxy
