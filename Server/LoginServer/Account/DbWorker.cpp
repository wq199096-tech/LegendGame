#include "Server/LoginServer/Account/DbWorker.h"

#include "Engine/Debug/Logger.h"

namespace legend::account {

DbWorker::~DbWorker() {
    Stop();
}

void DbWorker::Start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_started) {
        return;
    }
    m_stopping = false;
    m_started = true;
    m_thread = std::thread([this] { Run(); });
}

void DbWorker::Stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started || m_stopping) {
            return;
        }
        m_stopping = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_started = false;
}

void DbWorker::Post(Task task) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) {
            return; // 关停中丢弃新任务（指令一百：Response 丢弃即可）
        }
        m_queue.push_back(std::move(task));
    }
    m_cv.notify_one();
}

void DbWorker::Flush() {
    // 阶段10.1 修复：必须同时满足 队列空 && 无执行中任务
    //（原实现只等 m_queue.empty()，任务正在执行时提前返回）。
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return m_queue.empty() && m_activeTasks == 0; });
}

void DbWorker::Run() {
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
            if (m_queue.empty()) {
                if (m_stopping) {
                    return;
                }
                continue;
            }
            task = std::move(m_queue.front());
            m_queue.pop_front();
            ++m_activeTasks; // 取任务即计数（Flush 依据）
            if (m_queue.empty()) {
                m_cv.notify_all(); // Flush 等待者重查（队列空但任务仍在执行）
            }
        }
        // 指令十二：任务异常不得杀死 Worker 线程、不得卡死 Flush
        try {
            task();
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("[DB] task exception: ") + e.what());
        } catch (...) {
            LOG_ERROR("[DB] task unknown exception");
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_activeTasks; // 无论成功/异常都递减
        }
        m_cv.notify_all(); // 唤醒 Flush/Stop 等待者
    }
}

} // namespace legend::account
