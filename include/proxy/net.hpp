#pragma once

#include <boost/asio.hpp>
#include <chrono>
#include <cstdint>
#include <expected>
#include <string>

#include "proxy/error.hpp"

namespace proxy {

// Резолвит host и подключается с таймаутом. Исполняется на executor вызывающей корутины.
// host передаётся по значению, чтобы не зависеть от времени жизни аргументов.
boost::asio::awaitable<std::expected<boost::asio::ip::tcp::socket, Error>> connect_with_timeout(
    std::string host, std::uint16_t port, std::chrono::milliseconds timeout);

}  // namespace proxy
