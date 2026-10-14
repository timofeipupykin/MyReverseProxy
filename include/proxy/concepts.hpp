#pragma once

#include <concepts>
#include <cstddef>

namespace proxy {

// Любой непрерывный буфер байтов: std::string, std::string_view,
// std::vector<std::byte>, std::span<const std::byte>, std::array<char, N> ...
template <typename T>
concept ByteBuffer = requires(const T& t) {
    { t.data() } -> std::convertible_to<const void*>;
    { t.size() } -> std::convertible_to<std::size_t>;
};

}  // namespace proxy
