#include "Engine/Debug/Logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace legend::debug {

std::mutex Logger::s_mutex;
std::ofstream Logger::s_file;

namespace {

const char* LevelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error:   return "ERROR";
    }
    return "INFO";
}

} // namespace

void Logger::Init(const std::string& logDirectory) {
    std::lock_guard<std::mutex> lock(s_mutex);

    std::error_code ec;
    std::filesystem::create_directories(logDirectory, ec);
    if (ec) {
        std::cerr << "[WARNING] Failed to create log directory: " << logDirectory << std::endl;
    }

    s_file.open(logDirectory + "/latest.log", std::ios::out | std::ios::trunc);
    if (!s_file.is_open()) {
        std::cerr << "[WARNING] Failed to open log file: " << logDirectory << "/latest.log" << std::endl;
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_file.is_open()) {
        s_file.flush();
        s_file.close();
    }
}

void Logger::Write(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(s_mutex);

    std::time_t now = std::time(nullptr);
    std::tm timeInfo{};
#ifdef _WIN32
    localtime_s(&timeInfo, &now);
#else
    localtime_r(&now, &timeInfo);
#endif

    std::ostringstream line;
    line << '[' << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S") << "] ["
         << LevelTag(level) << "] " << message << '\n';

    const std::string text = line.str();
    std::cout << text;
    std::cout.flush();

    if (s_file.is_open()) {
        s_file << text;
        s_file.flush();
    }
}

void Logger::Debug(const std::string& message)   { Write(LogLevel::Debug, message); }
void Logger::Info(const std::string& message)    { Write(LogLevel::Info, message); }
void Logger::Warning(const std::string& message) { Write(LogLevel::Warning, message); }
void Logger::Error(const std::string& message)   { Write(LogLevel::Error, message); }

} // namespace legend::debug
