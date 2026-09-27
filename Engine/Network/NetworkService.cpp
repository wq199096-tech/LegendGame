#include "Engine/Network/NetworkService.h"

namespace legend::net {

void NetworkService::Start() {
    if (m_running.load()) {
        return;
    }
    m_work = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
        asio::make_work_guard(m_io));
    m_running.store(true);
    // 指令一百二十：io_context.run() 阻塞式事件循环（非 busy poll），
    // work_guard 保证无任务时不退出；Stop 时由 io_context.stop() 收尾
    m_thread = std::thread([this]() {
        try {
            m_io.run(); // 指令一百四十九：异常兜底
        } catch (const std::exception&) {
            // 网络线程顶层 catch：禁止异常逃逸导致整个进程退出
        }
    });
}

void NetworkService::Stop() {
    if (!m_running.exchange(false)) {
        return;
    }
    m_work.reset();
    m_io.stop();
    // 指令一百三十五：joinable 检查 + join，禁止 std::terminate
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_io.restart();
}

void NetworkService::Post(std::function<void()> fn) {
    asio::post(m_io, std::move(fn));
}

} // namespace legend::net
