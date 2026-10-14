#pragma once

#include <boost/asio.hpp>
#include <cstddef>
#include <thread>
#include <vector>

namespace proxy {

// Пул потоков на std::jthread, каждый поток крутит общий io_context.
// Остановка - кооперативная через std::stop_token, деструктор делает join (RAII).
class IoThreadPool {
public:
    explicit IoThreadPool(std::size_t threads);
    ~IoThreadPool();

    IoThreadPool(const IoThreadPool&) = delete;
    IoThreadPool& operator=(const IoThreadPool&) = delete;

    [[nodiscard]] boost::asio::io_context& context() noexcept { return ctx_; }
    [[nodiscard]] std::size_t size() const noexcept { return threads_.size(); }

    void stop();

private:
    boost::asio::io_context ctx_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard_;
    std::vector<std::jthread> threads_;  // объявлен последним => уничтожается первым
};

}  // namespace proxy
