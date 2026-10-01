#include "Server/WorldServer/WorldServer.h"
#include "Server/Common/ServerConfig.h"
#include "Server/Common/LogClient.h"

#include "Engine/Debug/Logger.h"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

// LegendWorldServer：纯 Console 世界服务（阶段11 指令六十）。
// 启动日志：[World] listening on 127.0.0.1:7200 / [World] Login link ready
int main(int argc, char** argv) {
    legend::world::WorldServer::Config config;
    std::string configPath = "Config/servers.json";
    legend::server::ServerConfig topology;
    std::string configError;
    if (legend::server::LoadServerConfig(configPath, topology, configError)) {
        config.listenPort=legend::server::FindService(topology,"world")->port;
        config.loginHost=legend::server::FindService(topology,"character")->host;
        config.loginPort=legend::server::FindService(topology,"character")->port;
        config.databasePath=topology.databasePath;
        config.positionSaveIntervalSeconds=topology.saveIntervalSeconds;
        // 阶段25.5：持久化统一走 DbServer RPC（World io 线程零 SQLite）。
        if (const auto* db = legend::server::FindService(topology, "db")) {
            config.dbHost = db->host;
            config.dbPort = db->port;
            config.databasePath = topology.databasePath;
        }
        config.serviceToken = topology.sharedSecret;
        config.dbTimeout = std::chrono::milliseconds(topology.rpcTimeoutMilliseconds);
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            configPath=argv[++i];
            if(!legend::server::LoadServerConfig(configPath,topology,configError)){std::fprintf(stderr,"Config error: %s\n",configError.c_str());return 1;}
            config.listenPort=legend::server::FindService(topology,"world")->port;
            config.loginHost=legend::server::FindService(topology,"character")->host;
            config.loginPort=legend::server::FindService(topology,"character")->port;
            config.databasePath=topology.databasePath;config.positionSaveIntervalSeconds=topology.saveIntervalSeconds;
            // 阶段25.5：持久化统一走 DbServer RPC（World io 线程零 SQLite）。
            if (const auto* db = legend::server::FindService(topology, "db")) {
                config.dbHost = db->host;
                config.dbPort = db->port;
            }
            config.serviceToken = topology.sharedSecret;
            config.dbTimeout = std::chrono::milliseconds(topology.rpcTimeoutMilliseconds);
        } else if (arg == "--port" && i + 1 < argc) {
            config.listenPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--login-port" && i + 1 < argc) {
            config.loginPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--db" && i + 1 < argc) {
            config.databasePath = argv[++i];
        } else {
            std::printf("Usage: LegendWorldServer.exe [--config Config/servers.json] [--port 7200] [--login-port 7400] "
                        "[--db data/legend_account.db]\n");
            return arg == "--help" || arg == "-h" ? 0 : 1;
        }
    }

    std::filesystem::create_directories("Logs/World");
    legend::debug::Logger::Init("Logs/World");
    LOG_INFO("[World] Service Name=LegendWorldServer Protocol=1 Listen=127.0.0.1:" +
             std::to_string(config.listenPort) + " Config=" + configPath +
             " Dependency Status=Degraded(waiting for CharacterServer/DbServer)");

    legend::net::NetworkService service;
    service.Start();

    std::shared_ptr<legend::server::LogClient> remoteLog;
    if (const auto* endpoint = legend::server::FindService(topology, "log")) {
        legend::server::LogClient::Config logConfig;
        logConfig.host=endpoint->host;logConfig.port=endpoint->port;
        logConfig.serviceType=legend::internal::ServiceType::WorldServer;
        logConfig.instanceId="world-1";logConfig.serviceToken=topology.sharedSecret;
        remoteLog=std::make_shared<legend::server::LogClient>(service,std::move(logConfig));remoteLog->Start();
    }

    auto world = std::make_shared<legend::world::WorldServer>(service);
    world->GetConfig() = config;
    world->SetHooks({
        .onLoginConnectionChanged =
            [](bool connected) {
                LOG_INFO(connected ? "[World] Login link ready" : "[World] Login link lost");
            },
        .onPlayerChanged =
            [remoteLog](std::uint64_t characterId, bool entered) {
                if (entered) {
                    LOG_INFO("[World] Player entered character=#" + std::to_string(characterId));
                }
                if (remoteLog) {
                    legend::internal::LogEvent event;
                    event.timestampMs=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                    event.service=legend::internal::ServiceType::WorldServer;event.level=2;
                    event.eventType=entered?legend::internal::LogEventType::WorldEnter:legend::internal::LogEventType::WorldLeave;
                    event.characterId=characterId;event.message=entered?"world entered":"world left";event.extraJson="{}";
                    remoteLog->Emit(std::move(event));
                }
            },
    });

    std::string error;
    if (!world->Start(error)) {
        LOG_ERROR("[World] Failed to start: " + error);
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

    LOG_INFO("[World] Shutting down.");
    world->Stop();
    if (remoteLog) remoteLog->Stop();
    service.Stop();
    legend::debug::Logger::Shutdown();
    return 0;
}
