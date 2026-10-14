#include "proxy/routing_table.hpp"

#include <utility>

namespace proxy {

RoutingTable::BackendList RoutingTable::snapshot() const {
    std::shared_lock lock(mutex_);
    return backends_;
}

std::size_t RoutingTable::size() const {
    std::shared_lock lock(mutex_);
    return backends_.size();
}

void RoutingTable::update(BackendList incoming) {
    // Сопоставление делаем вне эксклюзивной блокировки.
    const auto current = snapshot();
    for (auto& fresh : incoming) {
        for (const auto& old : current) {
            if (old->host == fresh->host && old->port == fresh->port) {
                fresh = old;
                break;
            }
        }
    }

    std::unique_lock lock(mutex_);  // держим лишь на время swap
    backends_ = std::move(incoming);
}

}  // namespace proxy
