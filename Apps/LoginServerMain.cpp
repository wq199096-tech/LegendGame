#include "Server/LoginServer/LoginServer.h"
#include "Server/Common/ServerConfig.h"
#include "Server/Common/LogClient.h"
#include "Server/AdminUi/ServerMainRunner.h"

#include "Engine/Debug/Logger.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace {

// Stage25.6：Login 管理台统计适配器（GUI 线程 Collect；weak_ptr 防悬垂）。
class LoginAdminStats : public legend::admin::IServiceProviderStats {
public:
    LoginAdminStats(std::weak_ptr<legend::login::LoginServer> server,
                    std::weak_ptr<legend::server::LogClient> remoteLog)
        : m_server(std::move(server)),
          m_remoteLog(std::move(remoteLog)),
          m_startTime(std::chrono::system_clock::now()) {}

    legend::admin::ServiceSnapshot Collect() override {
        legend::admin::ServiceSnapshot s;
        s.startTime = m_startTime;
        s.uptimeSeconds =
            std::chrono::duration<double>(std::chrono::system_clock::now() - m_startTime)
                .count();
        auto server = m_server.lock();
        if (!server) {
            return s;
        }
        s.running = true;
        const auto ls = server->CollectStats();
        s.listenIp = "127.0.0.1";
        s.listenPort = 7100;
        s.connectionCount = ls.gatewayCount;
        s.requestCount = ls.authRequests + ls.accountRequests;
        s.packetsReceived = ls.packetsReceived;
        s.packetsSent = ls.packetsSent;

        // 需求7：Login 专属数据
        s.extra.emplace_back("登录连接(Gateway)", std::to_string(ls.gatewayCount));
        s.extra.emplace_back("认证成功", std::to_string(ls.authSuccess));
        s.extra.emplace_back("认证失败", std::to_string(ls.authFail));
        s.extra.emplace_back("Session 创建次数", std::to_string(ls.sessionsCreated));
        s.extra.emplace_back("Ticket 签发", std::to_string(ls.ticketsIssued));
        s.extra.emplace_back("Ticket 消费", std::to_string(ls.ticketsConsumed));
        s.extra.emplace_back("Ticket 未消费", std::to_string(ls.ticketsLive));
        s.extra.emplace_back("账号链路请求", std::to_string(ls.accountRequests));
        s.extra.emplace_back("性能·收包数", std::to_string(ls.packetsReceived));
        s.extra.emplace_back("性能·发包数", std::to_string(ls.packetsSent));

        // 需求6/7：依赖服务状态
        if (ls.dbRemote) {
            s.dependencies.push_back({"DbServer", ls.dbAvailable, false,
                                      ls.dbAvailable ? "PersistenceClient 已连接"
                                                     : "连接断开，自动重连中"});
        } else {
            s.dependencies.push_back({"本地 SQLite", ls.dbAvailable, false,
                                      ls.dbAvailable ? "legacy 隔离模式" : "数据库未打开"});
        }
        if (auto remoteLog = m_remoteLog.lock()) {
            const bool logOk = remoteLog->IsAvailable();
            s.dependencies.push_back({"LogServer", logOk, false,
                                      logOk ? "审计日志上报中" : "连接断开（本地缓冲重发）"});
        }
        return s;
    }

private:
    std::weak_ptr<legend::login::LoginServer> m_server;
    std::weak_ptr<legend::server::LogClient> m_remoteLog;
    std::chrono::system_clock::time_point m_startTime;
};

int LoginServerMain(const legend::admin::ServerMainParams& params,
                    legend::admin::ServerAdminContext& context, int argc, char** argv) {
    legend::login::LoginServer::Config config;
    std::string configPath = params.configPath;
    legend::server::ServerConfig topology;
    std::string configError;
    if (legend::server::LoadServerConfig(configPath, topology, configError)) {
        config.listenPort = legend::server::FindService(topology, "login")->port;
        config.databasePath = topology.databasePath;
        const auto* db = legend::server::FindService(topology, "db");
        config.dbHost = db->host; config.dbPort = db->port;
        config.dbTimeout = std::chrono::milliseconds(topology.rpcTimeoutMilliseconds);
        config.serviceToken = topology.sharedSecret;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
            if (!legend::server::LoadServerConfig(configPath, topology, configError)) {
                std::fprintf(stderr, "Config error: %s\n", configError.c_str()); return 1;
            }
            config.listenPort=legend::server::FindService(topology,"login")->port;
            config.databasePath=topology.databasePath;
            const auto* db=legend::server::FindService(topology,"db");
            config.dbHost=db->host;config.dbPort=db->port;
            config.dbTimeout=std::chrono::milliseconds(topology.rpcTimeoutMilliseconds);
            config.serviceToken=topology.sharedSecret;
        } else if (arg == "--console" || arg == "--hidden") {
            continue; // Runner 模式开关：业务层无感
        } else if (arg == "--port" && i + 1 < argc) {
            config.listenPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else {
            std::printf("Usage: LegendLoginServer.exe [--config Config/servers.json] [--port 7100]\n");
            return arg == "--help" || arg == "-h" ? 0 : 1;
        }
    }

    std::filesystem::create_directories("Logs/Login");
    legend::debug::Logger::Init("Logs/Login");
    if (!params.console) {
        context.AttachLogCapture(); // Stage25.6：GUI 实时日志面板
    }
    LOG_INFO("[Login] Service Name=LegendLoginServer Protocol=1 Listen=127.0.0.1:" +
             std::to_string(config.listenPort) + " Config=" + configPath +
             " Dependency Status=Healthy");

    legend::net::NetworkService service;
    service.Start();

    std::shared_ptr<legend::server::LogClient> remoteLog;
    if (const auto* endpoint = legend::server::FindService(topology, "log")) {
        legend::server::LogClient::Config logConfig;
        logConfig.host=endpoint->host;logConfig.port=endpoint->port;
        logConfig.serviceType=legend::internal::ServiceType::LoginServer;
        logConfig.instanceId="login-1";logConfig.serviceToken=topology.sharedSecret;
        remoteLog=std::make_shared<legend::server::LogClient>(service,std::move(logConfig));
        remoteLog->Start();
    }

    auto login = std::make_shared<legend::login::LoginServer>(service);
    context.SetStatsProvider(
        std::make_shared<LoginAdminStats>(login, remoteLog));
    login->GetConfig() = config;
    login->SetHooks({
        .onGatewayConnectionChanged =
            [](bool connected) { LOG_INFO(connected ? "[Login] Gateway connected."
                                                    : "[Login] Gateway disconnected."); },
        .onAuthResult =
            [](std::uint64_t requestId, const std::string& username, bool success,
               std::uint64_t accountId) {
                LOG_INFO(std::string("[Login] Auth request id=") + std::to_string(requestId) +
                         " user=" + username + " -> " +
                         (success ? "success account=" + std::to_string(accountId)
                                  : std::string("rejected")));
            },
        // 阶段10 指令六十九：只记录 messageId/accountId/结果，绝不含 password/token/ticket
        .onAccountResult =
            [remoteLog](std::uint16_t innerMessageId, std::uint64_t accountId, bool success) {
                LOG_INFO(std::string("[Login] Account result msg=") +
                         legend::network::MessageIdName(innerMessageId) + " account=" +
                         std::to_string(accountId) + " -> " + (success ? "success" : "failed"));
                if (remoteLog && (innerMessageId == static_cast<std::uint16_t>(legend::network::MessageId::AccountLoginResponse))) {
                    legend::internal::LogEvent event;
                    event.timestampMs=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                    event.service=legend::internal::ServiceType::LoginServer;event.level=success?2:3;
                    event.eventType=success?legend::internal::LogEventType::LoginSuccess:legend::internal::LogEventType::LoginFailure;
                    event.accountId=accountId;event.message=success?"login accepted":"login rejected";event.extraJson="{}";
                    remoteLog->Emit(std::move(event));
                }
            },
    });

    std::string error;
    if (!login->Start(error)) {
        LOG_ERROR("[Login] Failed to listen: " + error);
        service.Stop();
        legend::debug::Logger::Shutdown();
        return 1;
    }

    // Ctrl+C（--console）与 GUI 关窗（externalStop）统一触发 Graceful Stop
    asio::io_context signals;
    asio::signal_set set(signals, SIGINT, SIGTERM);
    std::atomic<bool> stopRequested{false};
    set.async_wait([&](const std::error_code&, int) { stopRequested.store(true); });

    while (!stopRequested.load() &&
           !(params.externalStop && params.externalStop->load())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    set.cancel();
    signals.stop();

    LOG_INFO("[Login] Shutting down.");
    login->Stop();
    if (remoteLog) remoteLog->Stop();
    service.Stop();
    legend::debug::Logger::Shutdown();
    return 0;
}

} // namespace

// Stage25.6：默认双击 = Windows GUI 管理窗口；--console = 控制台模式（CI/无人值守）。
int main(int argc, char** argv) {
    return legend::admin::RunServerMain(argc, argv, legend::admin::ServerRole::Login,
                                        LoginServerMain);
}
