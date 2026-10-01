#include "Server/Common/ServerConfig.h"
#include "Server/Db/DbServer.h"
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

// Stage25.6：Db 管理台统计适配器（GUI 线程 Collect；weak_ptr 防悬垂）。
class DbAdminStats : public legend::admin::IServiceProviderStats {
public:
    DbAdminStats(std::weak_ptr<legend::db::DbServer> server)
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
            const auto db = server->CollectStats();
            s.listenIp = "127.0.0.1";
            s.listenPort = 7500;
            s.connectionCount = db.connectionCount;
            s.requestCount = db.queries + db.writes;
            s.packetsReceived = db.packetsReceived;
            s.packetsSent = db.packetsSent;

            // 需求7：Db 专属数据
            s.extra.emplace_back("SQLite 状态", db.dbOpen ? "已打开" : "未打开");
            s.extra.emplace_back("DB 路径", db.databasePath);
            s.extra.emplace_back("DbWorker 状态", db.workerRunning ? "运行中" : "未运行");
            s.extra.emplace_back("查询数", std::to_string(db.queries));
            s.extra.emplace_back("写入数", std::to_string(db.writes));
            s.extra.emplace_back("事务数", std::to_string(db.transactions));
            s.extra.emplace_back("失败数", std::to_string(db.failures));
            s.extra.emplace_back("性能·Queue长度", std::to_string(db.queueLength));
            s.extra.emplace_back("性能·收包数", std::to_string(db.packetsReceived));
            s.extra.emplace_back("性能·发包数", std::to_string(db.packetsSent));
            s.extra.emplace_back("Migration 版本", db.schemaVersion > 0
                                                     ? "v" + std::to_string(db.schemaVersion)
                                                     : std::string("初始化中"));
            s.extra.emplace_back("配置·DB路径", db.databasePath);
        }
        return s;
    }

private:
    std::weak_ptr<legend::db::DbServer> m_server;
    std::chrono::system_clock::time_point m_startTime;
};

int DbServerMain(const legend::admin::ServerMainParams& params,
                 legend::admin::ServerAdminContext& context, int argc, char** argv) {
    std::string configPath = params.configPath;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) configPath = argv[++i];
        else if (arg == "--console" || arg == "--hidden") { continue; }
        else { std::printf("Usage: LegendDbServer.exe [--config Config/servers.json]\n"); return 1; }
    }
    legend::server::ServerConfig topology; std::string error;
    if (!legend::server::LoadServerConfig(configPath, topology, error)) {
        std::fprintf(stderr, "[Db] config error: %s\n", error.c_str()); return 1;
    }
    const auto* endpoint = legend::server::FindService(topology, "db");
    std::filesystem::create_directories("Logs/Db"); legend::debug::Logger::Init("Logs/Db");
    if (!params.console) {
        context.AttachLogCapture(); // Stage25.6：GUI 实时日志面板
    }
    LOG_INFO("[Db] Service Name=LegendDbServer Version=" + std::string(LEGEND_ENGINE_VERSION) +
             " Protocol=1 Listen=" + endpoint->host + ":" + std::to_string(endpoint->port) +
             " Config=" + configPath + " Dependency Status=Healthy");
    legend::net::NetworkService network; network.Start();
    auto server = std::make_shared<legend::db::DbServer>(network);
    context.SetStatsProvider(std::make_shared<DbAdminStats>(server));
    server->GetConfig().listenPort = endpoint->port;
    server->GetConfig().databasePath = topology.databasePath;
    server->GetConfig().serviceToken = topology.sharedSecret;
    if (!server->Start(error)) { LOG_ERROR("[Db] start failed on port " +
        std::to_string(endpoint->port) + ": " + error); network.Stop(); return 1; }
    asio::io_context signals; asio::signal_set set(signals, SIGINT, SIGTERM);
    std::atomic<bool> stop{false}; set.async_wait([&](const std::error_code&, int) { stop.store(true); });
    while (!stop.load() && !(params.externalStop && params.externalStop->load()))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_INFO("[Db] graceful shutdown"); server->Stop(); network.Stop();
    legend::debug::Logger::Shutdown(); return 0;
}

} // namespace

// Stage25.6：默认双击 = Windows GUI 管理窗口；--console = 控制台模式（CI/无人值守）。
int main(int argc, char** argv) {
    return legend::admin::RunServerMain(argc, argv, legend::admin::ServerRole::Db,
                                        DbServerMain);
}
