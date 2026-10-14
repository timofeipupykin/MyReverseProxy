#pragma once

#include <string_view>

namespace proxy {

// Коды ошибок. Используются вместе с std::expected вместо исключений.
enum class Error {
    IncompleteRequest,     // заголовки ещё не дочитаны
    MalformedRequestLine,  // кривая стартовая строка
    MalformedHeader,       // кривой заголовок
    HeaderTooLarge,        // заголовки больше лимита
    NoHealthyBackend,      // нет живых бэкендов
    BackendConnectFailed,  // не удалось соединиться с бэкендом
    BackendTimeout,        // таймаут соединения с бэкендом
    InvalidConfig,         // ошибка в конфигурации
};

[[nodiscard]] constexpr std::string_view to_string(Error e) noexcept {
    switch (e) {
        case Error::IncompleteRequest:    return "incomplete request";
        case Error::MalformedRequestLine: return "malformed request line";
        case Error::MalformedHeader:      return "malformed header";
        case Error::HeaderTooLarge:       return "header too large";
        case Error::NoHealthyBackend:     return "no healthy backend";
        case Error::BackendConnectFailed: return "backend connect failed";
        case Error::BackendTimeout:       return "backend connect timeout";
        case Error::InvalidConfig:        return "invalid config";
    }
    return "unknown";
}

}  // namespace proxy
