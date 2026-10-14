#pragma once

#include <cstdio>
#include <format>
#include <mutex>
#include <print>
#include <utility>

namespace proxy {

namespace detail {
inline std::mutex& log_mutex() {
    static std::mutex m;
    return m;
}
}  // namespace detail

// Потокобезопасное логирование через C++23 std::println.
template <typename... Args>
void log(std::format_string<Args...> fmt, Args&&... args) {
    std::lock_guard lock(detail::log_mutex());
    std::println(stderr, fmt, std::forward<Args>(args)...);
}

}  // namespace proxy
