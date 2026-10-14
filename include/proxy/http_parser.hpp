#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string_view>
#include <vector>

#include "proxy/concepts.hpp"
#include "proxy/error.hpp"

namespace proxy {

struct Header {
    std::string_view name;
    std::string_view value;
};

// Zero-copy: все string_view указывают внутрь исходного буфера,
// поэтому буфер должен жить и не меняться, пока используется HttpRequestHead.
struct HttpRequestHead {
    std::string_view method;
    std::string_view target;
    std::string_view version;
    std::vector<Header> headers;
    std::size_t head_size = 0;  // размер заголовков вместе с завершающим CRLFCRLF

    // Поиск заголовка без учёта регистра.
    [[nodiscard]] std::optional<std::string_view> find_header(std::string_view name) const;
};

inline constexpr std::size_t kMaxHeadSize = 16 * 1024;

// Разбирает начало HTTP-запроса. Ошибки возвращаются через std::expected.
// Error::IncompleteRequest означает "нужно дочитать ещё данных".
[[nodiscard]] std::expected<HttpRequestHead, Error> parse_request_head(std::string_view data);

// Перегрузка для любых байтовых буферов (std::span<const std::byte>, vector<char> ...).
template <ByteBuffer B>
    requires(!std::convertible_to<const B&, std::string_view>)
[[nodiscard]] std::expected<HttpRequestHead, Error> parse_request_head(const B& buf) {
    return parse_request_head(
        std::string_view{reinterpret_cast<const char*>(buf.data()), static_cast<std::size_t>(buf.size())});
}

}  // namespace proxy
