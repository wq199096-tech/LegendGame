#include "Server/Character/CharacterServer.h"
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

// Stage25.6：Character 管理台统计适配器（GUI 线程 Collect；weak_ptr 防悬垂）。
class CharacterAdminStats : public legend::admin::IServiceProviderStats {
public:
    CharacterAdminStats(std::weak_ptr<legend::character::CharacterServer> server)
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
            const auto cs = server->CollectStats();
            s.listenIp = "127.0.0.1";
            s.listenPort = 7400;
            s.connectionCount = cs.connectionCount;
            s.requestCount = cs.listRequests + cs.createRequests + cs.deleteRequests +
                             cs.selectRequests;
            s.packetsReceived = cs.packetsReceived;
            s.packetsSent = cs.packetsSent;

            // 需求7：Character 专属数据
            s.extra.emplace_back("角色列表请求", std::to_string(cs.listRequests));
            s.extra.emplace_back("创建请求", std::to_string(cs.createRequests));
            s.extra.emplace_back("创建成功", std::to_string(cs.createSuccess));
            s.extra.emplace_back("删除请求", std::to_string(cs.deleteRequests));
            s.extra.emplace_back("删除成功", std::to_string(cs.deleteSuccess));
            s.extra.emplace_back("当前选择请求", std::to_string(cs.selectRequests));
            s.extra.emplace_back("Ticket 签发", std::to_string(cs.ticketsIssued));
            s.extra.emplace_back("Ticket 消费", std::to_string(cs.ticketsConsumed));
            s.extra.emplace_back("性能·收包数", std::to_string(cs.packetsReceived));
            s.extra.emplace_back("性能·发包数", std::to_string(cs.packetsSent));

            // 需求6/7：依赖服务状态
            const bool dbOk = cs.dbAvailable;
            s.dependencies.push_back({"DbServer", dbOk, false,
                                      dbOk ? "PersistenceClient 已连接" : "连接断开，自动重连中"});
            const bool logOk = server->IsLogConnected();
            s.dependencies.push_back({"LogServer", logOk, false,
                                      logOk ? "审计日志上报中" : "连接断开（本地缓冲重发）"});
        }
        return s;
    }

private:
    std::weak_ptr<legend::character::CharacterServer> m_server;
    std::chrono::system_clock::time_point m_startTime;
};

int CharacterServerMain(const legend::admin::ServerMainParams& params,
                        legend::admin::ServerAdminContext& context, int argc, char** argv) {
    std::string configPath = params.configPath;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) configPath = argv[++i];
        else if (arg == "--console" || arg == "--hidden") { continue; }
        else { std::printf("Usage: LegendCharacterServer.exe [--config Config/servers.json]\n"); return 1; }
    }
    legend::server::ServerConfig topology; std::string error;
    if (!legend::server::LoadServerConfig(configPath, topology, error)) {
        std::fprintf(stderr, "[Character] config error: %s\n", error.c_str()); return 1;
    }
    auto character = legend::server::FindService(topology, "character");
    auto db = legend::server::FindService(topology, "db");
    auto log = legend::server::FindService(topology, "log");
    std::filesystem::create_directories("Logs/Character");
    legend::debug::Logger::Init("Logs/Character");
    if (!params.console) {
        context.AttachLogCapture(); // Stage25.6：GUI 实时日志面板
    }
    LOG_INFO("[Character] Service Name=LegendCharacterServer Version=" +
             std::string(LEGEND_ENGINE_VERSION) + " Protocol=1 Listen=" + character->host +
             ":" + std::to_string(character->port) + " Config=" + configPath +
             " Dependency Status=Degraded(waiting for DbServer)");
    legend::net::NetworkService network; network.Start();
    auto server = std::make_shared<legend::character::CharacterServer>(network);
    context.SetStatsProvider(std::make_shared<CharacterAdminStats>(server));
    server->GetConfig().listenPort = character->port;
    server->GetConfig().dbHost = db->host;
    server->GetConfig().dbPort = db->port;
    server->GetConfig().logHost = log->host;
    server->GetConfig().logPort = log->port;
    server->GetConfig().serviceToken = topology.sharedSecret;
    server->GetConfig().dbTimeout = std::chrono::milliseconds(topology.rpcTimeoutMilliseconds);
    if (!server->Start(error)) { LOG_ERROR("[Character] start failed on port " +
        std::to_string(character->port) + ": " + error); network.Stop(); return 1; }
    asio::io_context signals; asio::signal_set set(signals, SIGINT, SIGTERM);
    std::atomic<bool> stop{false}; set.async_wait([&](const std::error_code&, int) { stop.store(true); });
    while (!stop.load() && !(params.externalStop && params.externalStop->load()))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_INFO("[Character] graceful shutdown"); server->Stop(); network.Stop();
    legend::debug::Logger::Shutdown(); return 0;
}

} // namespace

// Stage25.6：默认双击 = Windows GUI 管理窗口；--console = 控制台模式（CI/无人值守）。
int main(int argc, char** argv) {
    return legend::admin::RunServerMain(argc, argv, legend::admin::ServerRole::Character,
                                        CharacterServerMain);
}
