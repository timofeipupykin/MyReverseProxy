# async-proxy — асинхронный обратный прокси и балансировщик нагрузки (C++23)

## Требования
- GCC 14+ или Clang 18+ (с libc++/libstdc++, где есть `std::print`, `std::expected`)
- CMake 3.20+
- Boost 1.80+ (используется только header-only Boost.Asio)

## Сборка и запуск
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure      # юнит-тесты

# три тестовых бэкенда
for p in 9001 9002 9003; do python3 -m http.server $p --directory /tmp >/dev/null 2>&1 & done

./build/async_proxy --port 8080 --config config/backends.conf --algo rr --threads 4
curl -i http://127.0.0.1:8080/
```

Параметры: `--port`, `--threads`, `--config`, `--algo rr|lc`.

- `Ctrl+C` / `SIGTERM` — мягкая остановка.
- `kill -HUP <pid>` — перечитать `backends.conf` без остановки сервера.
- Отладка гонок: `-DPROXY_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug`.

## Структура
| Файл | Что демонстрирует |
|---|---|
| `thread_pool.*` | пул на `std::jthread`, остановка через `std::stop_token` / `stop_callback` |
| `health_checker.*` | фоновый `jthread`, `condition_variable_any::wait_for(lock, stop_token, ...)` |
| `proxy_server.*` | корутины `asio::awaitable`, strand на соединение, relay в обе стороны |
| `net.*` | connect с таймаутом через `awaitable_operators` (`\|\|`) |
| `http_parser.*` | zero-copy разбор на `std::string_view`, ошибки через `std::expected` |
| `routing_table.*` | `std::shared_mutex`: много читателей, редкий писатель |
| `load_balancer.*` | `concept BalancingStrategy`, Round-Robin / Least Connections, `std::variant` |
| `concepts.hpp` | `concept ByteBuffer` для перегрузки парсера |
| `logger.hpp` | потокобезопасный `std::println` |

## Ограничения (что можно доработать)
- Прокси не переписывает заголовки: после первого запроса соединение с клиентом
  «приклеивается» к одному бэкенду (keep-alive работает, но балансировка идёт на уровне соединений).
- Нет таймаута на чтение заголовков от клиента (защита от slowloris) и на простой relay.
- Нет весов, TLS, метрик (Prometheus) и admin-API — хорошие кандидаты для расширения.
