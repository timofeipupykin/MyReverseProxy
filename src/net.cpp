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

    const auto [resolve_ec, endpoints] =
        co_await resolver.async_resolve(host, std::to_string(port), asio::as_tuple(asio::use_awaitable));
    if (resolve_ec) co_return std::unexpected(Error::BackendConnectFailed);

    // Кто завершился первым - тот и отменяет второго. operator|| ждёт первую *успешную*
    // операцию, поэтому ошибки возвращаем значением (as_tuple), а не исключением:
    // иначе отказ в соединении превращался бы в таймаут после полного ожидания.
    auto result = co_await (asio::async_connect(socket, endpoints, asio::as_tuple(asio::use_awaitable)) ||
                            timer.async_wait(asio::as_tuple(asio::use_awaitable)));
    if (result.index() == 1) co_return std::unexpected(Error::BackendTimeout);
    if (std::get<0>(std::get<0>(result))) co_return std::unexpected(Error::BackendConnectFailed);

    boost::system::error_code ec;
    socket.set_option(tcp::no_delay(true), ec);
    co_return std::move(socket);
}

}  // namespace proxy
