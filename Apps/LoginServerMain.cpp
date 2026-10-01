#include "Server/LoginServer/LoginServer.h"
#include "Server/Common/ServerConfig.h"
#include "Server/Common/LogClient.h"

#include "Engine/Debug/Logger.h"

#include <filesystem>
#include <string>

// LegendLoginServer：纯 Console 认证服务（阶段9 指令五十三/八十六/一百二十三）。
int main(int argc, char** argv) {
    legend::login::LoginServer::Config config;
    std::string configPath = "Config/servers.json";
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
        } else if (arg == "--port" && i + 1 < argc) {
            config.listenPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else {
            std::printf("Usage: LegendLoginServer.exe [--config Config/servers.json] [--port 7100]\n");
            return arg == "--help" || arg == "-h" ? 0 : 1;
        }
    }

    std::filesystem::create_directories("Logs/Login");
    legend::debug::Logger::Init("Logs/Login");
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

    asio::io_context signals;
    asio::signal_set set(signals, SIGINT, SIGTERM);
    std::atomic<bool> stopRequested{false};
    set.async_wait([&](const std::error_code&, int) { stopRequested.store(true); });

    while (!stopRequested.load()) {
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
