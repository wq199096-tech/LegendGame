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
    // 阻塞直到队列清空（测试/关停用）。
    void Flush();

private:
    void Run();

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Task> m_queue;
    bool m_started = false;
    bool m_stopping = false;
};

} // namespace legend::account
