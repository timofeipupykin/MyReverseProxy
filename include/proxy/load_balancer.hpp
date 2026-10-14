#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <span>
#include <variant>

#include "proxy/backend.hpp"

namespace proxy {

using BackendSpan = std::span<const BackendPtr>;

// Стратегия балансировки: pick() выбирает живой бэкенд или возвращает nullptr.
template <typename S>
concept BalancingStrategy = requires(const S& s, BackendSpan backends) {
    { s.pick(backends) } -> std::same_as<BackendPtr>;
};

class RoundRobin {
public:
    RoundRobin() = default;
    RoundRobin(RoundRobin&& other) noexcept
        : counter_(other.counter_.load(std::memory_order_relaxed)) {}
    RoundRobin(const RoundRobin&) = delete;

    [[nodiscard]] BackendPtr pick(BackendSpan backends) const;

private:
    mutable std::atomic<std::size_t> counter_{0};
};

class LeastConnections {
public:
    [[nodiscard]] BackendPtr pick(BackendSpan backends) const;
};

enum class Algorithm { RoundRobin, LeastConnections };

// Выбор алгоритма в рантайме без виртуальных вызовов (std::variant + std::visit).
class Balancer {
public:
    explicit Balancer(Algorithm algo);

    [[nodiscard]] BackendPtr pick(BackendSpan backends) const {
        return std::visit([&](const auto& s) { return s.pick(backends); }, impl_);
    }

private:
    std::variant<RoundRobin, LeastConnections> impl_;
};

static_assert(BalancingStrategy<RoundRobin>);
static_assert(BalancingStrategy<LeastConnections>);
static_assert(BalancingStrategy<Balancer>);

}  // namespace proxy
