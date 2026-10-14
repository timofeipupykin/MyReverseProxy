#include "proxy/load_balancer.hpp"

namespace proxy {

BackendPtr RoundRobin::pick(BackendSpan backends) const {
    const auto n = backends.size();
    if (n == 0) return nullptr;

    const auto start = counter_.fetch_add(1, std::memory_order_relaxed);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& candidate = backends[(start + i) % n];
        if (candidate->healthy.load(std::memory_order_relaxed)) return candidate;
    }
    return nullptr;
}

BackendPtr LeastConnections::pick(BackendSpan backends) const {
    BackendPtr best;
    std::size_t best_load = 0;
    for (const auto& b : backends) {
        if (!b->healthy.load(std::memory_order_relaxed)) continue;
        const auto load = b->active_connections.load(std::memory_order_relaxed);
        if (!best || load < best_load) {
            best = b;
            best_load = load;
        }
    }
    return best;
}

Balancer::Balancer(Algorithm algo)
    : impl_(algo == Algorithm::RoundRobin
                ? std::variant<RoundRobin, LeastConnections>{std::in_place_type<RoundRobin>}
                : std::variant<RoundRobin, LeastConnections>{std::in_place_type<LeastConnections>}) {}

}  // namespace proxy
