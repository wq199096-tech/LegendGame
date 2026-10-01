#include "Server/AdminUi/LogCapture.h"

namespace legend::admin {

void LogCapture::Attach() {
    // 注意：sink 在 Logger 内部锁内触发，Push 里禁止调用 Logger API
    legend::debug::Logger::SetSink(
        [this](legend::debug::LogLevel level, const std::string& formattedLine) {
            Push(static_cast<int>(level), formattedLine);
        });
}

void LogCapture::Detach() {
    legend::debug::Logger::ClearSink();
}

void LogCapture::Push(int level, std::string formattedLine) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (level == static_cast<int>(legend::debug::LogLevel::Warning)) {
        ++m_warn;
    } else if (level == static_cast<int>(legend::debug::LogLevel::Error)) {
        ++m_error;
    }
    if (m_lines.size() >= kCapacity) {
        m_lines.pop_front();
    }
    m_lines.push_back(Line{level, std::move(formattedLine)});
}

std::vector<LogCapture::Line> LogCapture::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return {m_lines.begin(), m_lines.end()};
}

void LogCapture::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lines.clear();
}

} // namespace legend::admin
