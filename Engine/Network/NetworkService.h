#pragma once

#include <asio.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace legend::net {

// 阶段9 指令二十九/三十/一百二十：NetworkService——io_context + work_guard +
// 专用网络线程（io_context.run 不 busy loop），Start/Stop/Post。
class NetworkService {
public:
    NetworkService() = default;
    ~NetworkService() { Stop(); }

    NetworkService(const NetworkService&) = delete;
    NetworkService& operator=(const NetworkService&) = delete;

    void Start();
    void Stop();
    void Post(std::function<void()> fn);
    asio::io_context& Io() { return m_io; }
    bool IsRunning() const { return m_running.load(); }

private:
    asio::io_context m_io;
    std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> m_work;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

} // namespace legend::net
