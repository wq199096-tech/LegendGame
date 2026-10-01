#include "Server/Log/LogServer.h"
#include "Server/Common/ServerConfig.h"
#include "Server/AdminUi/ServerMainRunner.h"
#include "Engine/Debug/Logger.h"
#include "Shared/Version.h"

#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

namespace {

// Stage25.6：Log 管理台统计适配器（GUI 线程 Collect；weak_ptr 防悬垂）。
class LogAdminStats : public legend::admin::IServiceProviderStats {
public:
    LogAdminStats(std::weak_ptr<legend::logserver::LogServer> server)
        : m_server(std::move(server)),
          m_startTime(std::chrono::system_clock::now()) {}

    legend::admin::ServiceSnapshot Collect() override {
        legend::admin::ServiceSnapshot s;
        s.startTime = m_startTime;
        s.uptimeSeconds =
            std::chrono::duration<double>(std::chrono::system_clock::now() - m_startTime)
                .count();
        if (auto server = m_server.lock()) {
            s.running = true;
            const auto log = server->CollectStats();
            s.listenIp = "127.0.0.1";
            s.listenPort = 7600;
            s.connectionCount = log.connectionCount;
            s.requestCount = log.receivedCount;
            s.packetsReceived = log.packetsReceived;
            s.packetsSent = log.packetsSent;

            // 需求7：Log 专属数据
            s.extra.emplace_back("接收日志数", std::to_string(log.receivedCount));
            s.extra.emplace_back("写入日志数", std::to_string(log.writtenCount));
            s.extra.emplace_back("性能·Queue长度", std::to_string(log.queuedCount));
            s.extra.emplace_back("丢弃数", std::to_string(log.droppedCount));
            s.extra.emplace_back("当前日志文件",
                                 log.currentFile.empty() ? std::string("（暂无）") : log.currentFile);
            s.extra.emplace_back("文件大小", FormatBytes(log.currentFileSize));
            s.extra.emplace_back("fallback 状态",
                                 log.writeFailed ? std::string("降级（曾发生写入失败）")
                                                 : std::string("正常"));
            s.extra.emplace_back("配置·日志根目录", m_logRoot);
            s.extra.emplace_back("性能·收包数", std::to_string(log.packetsReceived));
            s.extra.emplace_back("性能·发包数", std::to_string(log.packetsSent));
        }
        return s;
    }

    void SetLogRoot(std::string root) { m_logRoot = std::move(root); }

private:
    static std::string FormatBytes(std::uint64_t bytes) {
        char buf[64];
        if (bytes >= 1024ull * 1024ull) {
            std::snprintf(buf, sizeof(buf), "%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        } else if (bytes >= 1024ull) {
            std::snprintf(buf, sizeof(buf), "%.2f KB", static_cast<double>(bytes) / 1024.0);
        } else {
            std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
        }
        return buf;
    }

    std::weak_ptr<legend::logserver::LogServer> m_server;
    std::chrono::system_clock::time_point m_startTime;
    std::string m_logRoot;
};

int LogServerMain(const legend::admin::ServerMainParams& params,
                  legend::admin::ServerAdminContext& context, int argc, char** argv) {
    std::string configPath = params.configPath;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) configPath = argv[++i];
        else if (arg == "--console" || arg == "--hidden") { continue; }
        else { std::printf("Usage: LegendLogServer.exe [--config Config/servers.json]\n"); return 1; }
    }
    legend::server::ServerConfig topology; std::string error;
    if (!legend::server::LoadServerConfig(configPath, topology, error)) {
        std::fprintf(stderr, "[Log] config error: %s\n", error.c_str()); return 1;
    }
    auto endpoint = legend::server::FindService(topology, "log");
    std::filesystem::create_directories("Logs/Log");
    legend::debug::Logger::Init("Logs/Log");
    if (!params.console) {
        context.AttachLogCapture(); // Stage25.6：GUI 实时日志面板
    }
    LOG_INFO("[Log] Service Name=LegendLogServer Version=" + std::string(LEGEND_ENGINE_VERSION) +
             " Protocol=1 Listen=" + endpoint->host + ":" + std::to_string(endpoint->port) +
             " Config=" + configPath + " Dependency Status=Healthy");
    legend::net::NetworkService network; network.Start();
    auto server = std::make_shared<legend::logserver::LogServer>(network);
    auto stats = std::make_shared<LogAdminStats>(server);
    stats->SetLogRoot(server->GetConfig().logRoot);
    context.SetStatsProvider(stats);
    server->GetConfig().listenPort = endpoint->port;
    server->GetConfig().serviceToken = topology.sharedSecret;
    if (!server->Start(error)) { LOG_ERROR("[Log] start failed on port " +
        std::to_string(endpoint->port) + ": " + error); network.Stop(); return 1; }
    asio::io_context signals; asio::signal_set set(signals, SIGINT, SIGTERM);
    std::atomic<bool> stop{false}; set.async_wait([&](const std::error_code&, int) { stop.store(true); });
    while (!stop.load() && !(params.externalStop && params.externalStop->load()))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_INFO("[Log] graceful shutdown"); server->Stop(); network.Stop();
    legend::debug::Logger::Shutdown(); return 0;
}

} // namespace

// Stage25.6：默认双击 = Windows GUI 管理窗口；--console = 控制台模式（CI/无人值守）。
int main(int argc, char** argv) {
    return legend::admin::RunServerMain(argc, argv, legend::admin::ServerRole::Log,
                                        LogServerMain);
}
