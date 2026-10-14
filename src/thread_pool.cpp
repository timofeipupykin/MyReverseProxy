#include "proxy/thread_pool.hpp"

#include <algorithm>
#include <exception>
#include <stop_token>

#include "proxy/logger.hpp"

namespace proxy {

IoThreadPool::IoThreadPool(std::size_t threads)
    : ctx_(static_cast<int>(std::max<std::size_t>(threads, 1))),
      guard_(boost::asio::make_work_guard(ctx_)) {
    const auto n = std::max<std::size_t>(threads, 1);
    threads_.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        threads_.emplace_back([this](std::stop_token st) {
            // Когда кто-то просит остановку - будим io_context.
            std::stop_callback on_stop(st, [this] { ctx_.stop(); });
            for (;;) {
                try {
                    ctx_.run();
                    break;
                } catch (const std::exception& e) {
                    log("worker: unhandled exception: {}", e.what());
                }
            }
        });
    }
}

IoThreadPool::~IoThreadPool() {
    stop();
    // jthread::~jthread сам вызовет request_stop() и join().
}

void IoThreadPool::stop() {
    guard_.reset();
    for (auto& t : threads_) t.request_stop();
}

}  // namespace proxy
