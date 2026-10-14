#pragma once

#include <expected>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "proxy/backend.hpp"
#include "proxy/error.hpp"
#include "proxy/load_balancer.hpp"

namespace proxy {

// Таблица маршрутизации, которую можно менять "на лету".
// Читатели (десятки потоков, разбирающих запросы) берут shared_lock,
// обновление берёт unique_lock лишь на время подмены вектора.
class RoutingTable {
public:
    using BackendList = std::vector<BackendPtr>;

    // Выбор бэкенда под shared_lock.
    template <BalancingStrategy S>
    [[nodiscard]] std::expected<BackendPtr, Error> select(const S& strategy) const {
        std::shared_lock lock(mutex_);
        if (BackendPtr b = strategy.pick(backends_)) return b;
        return std::unexpected(Error::NoHealthyBackend);
    }

    // Копия списка (для health checker'а).
    [[nodiscard]] BackendList snapshot() const;

    // Атомарная замена списка. Для бэкендов с тем же host:port переиспользуется
    // существующий объект, чтобы не потерять статус здоровья и счётчики соединений.
    void update(BackendList incoming);

    [[nodiscard]] std::size_t size() const;

private:
    mutable std::shared_mutex mutex_;
    BackendList backends_;
};

}  // namespace proxy
