#pragma once

#include <mutex>
#include <string>
#include <fstream>

namespace legend::debug {

enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
};

// 全局日志：同时输出到控制台与 Logs/latest.log
class Logger {
public:
    static void Init(const std::string& logDirectory = "Logs");
    static void Shutdown();

    static void Debug(const std::string& message);
    static void Info(const std::string& message);
    static void Warning(const std::string& message);
    static void Error(const std::string& message);

private:
    static void Write(LogLevel level, const std::string& message);

    static std::mutex s_mutex;
    static std::ofstream s_file;
};

} // namespace legend::debug

#define LOG_DEBUG(msg) ::legend::debug::Logger::Debug(msg)
#define LOG_INFO(msg) ::legend::debug::Logger::Info(msg)
#define LOG_WARN(msg) ::legend::debug::Logger::Warning(msg)
#define LOG_ERROR(msg) ::legend::debug::Logger::Error(msg)
