#pragma once

#include <string_view>

namespace proxy::detail {

[[nodiscard]] constexpr std::string_view trim(std::string_view s) noexcept {
    constexpr std::string_view ws = " \t\r\n";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string_view::npos) return {};
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

}  // namespace proxy::detail
