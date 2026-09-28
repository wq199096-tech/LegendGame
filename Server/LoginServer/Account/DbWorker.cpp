#include "Server/LoginServer/Account/DbWorker.h"

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
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] { return m_queue.empty(); });
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
            if (m_queue.empty()) {
                m_cv.notify_all(); // Flush 等待者唤醒
            }
        }
        task();
    }
}

} // namespace legend::account
