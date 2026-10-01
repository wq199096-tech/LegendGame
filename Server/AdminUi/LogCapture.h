#pragma once

#include "Engine/Debug/Logger.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace legend::admin {

// 进程内日志捕获：Logger sink -> 环形缓冲 -> GUI 日志面板。
// sink 在 Logger 内部锁内回调：Push 不得再调用任何 Logger API（防死锁）。
class LogCapture {
public:
    struct Line {
        int level = 1;          // legend::debug::LogLevel
        std::string text;       // 已格式化整行（含时间戳与级别标签）
    };

    // 挂到全局 Logger（GUI 模式启动时调用；--console 不挂，保持零开销）
    void Attach();
    void Detach();

    // sink 入口（Logger 锁内调用）
    void Push(int level, std::string formattedLine);

    // GUI 线程读取
    std::vector<Line> Snapshot() const;
    void Clear();
    std::uint64_t WarnCount() const { return m_warn.load(std::memory_order_relaxed); }
    std::uint64_t ErrorCount() const { return m_error.load(std::memory_order_relaxed); }

    static constexpr std::size_t kCapacity = 4096;

private:
    mutable std::mutex m_mutex;
    std::deque<Line> m_lines;
    std::atomic<std::uint64_t> m_warn{0};
    std::atomic<std::uint64_t> m_error{0};
};

} // namespace legend::admin
