#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace legend::account {

// 阶段10 指令六十三/六十四：单线程 DB Worker。
// Network 线程 -> Post(task) -> DB 线程执行 -> 结果由任务自行 post 回 io_context。
// 禁止在 io_context 线程直接执行 SQLite（避免阻塞事件循环）。
class DbWorker {
public:
    using Task = std::function<void()>;

    DbWorker() = default;
    ~DbWorker();

    DbWorker(const DbWorker&) = delete;
    DbWorker& operator=(const DbWorker&) = delete;

    void Start();
    // 停止：先等待队列内任务执行完（flush 语义），再 join 线程（指令六十六）。
    void Stop();
    void Post(Task task);
    // 阻塞直到 队列空 && 无执行中任务（阶段10.1 修复提前返回）。
    void Flush();
    // Stage25.6 服务器管理台：当前待执行任务数（GUI 线程随时可读）。
    std::size_t QueueLength() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }
    // Stage25.6 服务器管理台：Worker 线程是否在运行。
    bool IsRunning() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_started && !m_stopping;
    }

private:
    void Run();

    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Task> m_queue;
    // 阶段10.1：执行中任务计数——Flush 必须等 队列空 && m_activeTasks == 0
    //（原实现只看队列空，任务正在执行时提前返回）
    std::size_t m_activeTasks = 0;
    bool m_started = false;
    bool m_stopping = false;
};

} // namespace legend::account
