#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace legend::admin {

// 依赖服务状态行（GUI 概览/连接页显示，Success/Warning/Error 语义色）
struct DependencyStatus {
    std::string name;    // 依赖名（如 "DbServer"、"LoginServer"）
    bool healthy = false;
    bool degraded = false;
    std::string detail;  // 补充说明（如 "未接入"）
};

// 每服统计快照：GUI 线程每 500ms 由 Collect() 拷贝一次，UI 全程无锁渲染。
struct ServiceSnapshot {
    std::string serviceCnName;
    bool running = false;
    std::chrono::system_clock::time_point startTime{};  // 本地启动时刻（可格式化显示）
    double uptimeSeconds = 0.0;                         // 运行时长（Collect 时刻）
    std::string listenIp;
    std::uint16_t listenPort = 0;

    std::uint64_t connectionCount = 0;
    std::uint64_t requestCount = 0;
    std::uint64_t warnCount = 0;
    std::uint64_t errorCount = 0;
    std::uint64_t packetsReceived = 0; // 监听侧累计收包
    std::uint64_t packetsSent = 0;     // 监听侧累计发包

    std::vector<DependencyStatus> dependencies;
    // 服务器类型专属数据（需求7）：中文键 -> 已格式化显示值。
    // 键前缀约定："性能·" 仅性能页；"会话状态·" 仅连接页；其余在概览页。
    std::vector<std::pair<std::string, std::string>> extra;
};

// 各服务器实现的只读统计适配接口（UI 零重复；每服一个 XxxAdminStats）。
// 实现必须线程安全（GUI 线程调用 Collect；业务线程写内部计数）。
class IServiceProviderStats {
public:
    virtual ~IServiceProviderStats() = default;
    virtual ServiceSnapshot Collect() = 0;
};

} // namespace legend::admin
