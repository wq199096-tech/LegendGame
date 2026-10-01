#include "Server/WorldServer/WorldServer.h"
#include "Server/Common/ServerConfig.h"
#include "Server/Common/LogClient.h"
#include "Server/AdminUi/ServerMainRunner.h"

#include "Engine/Debug/Logger.h"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace {

// Stage25.6：World 管理台统计适配器（GUI 线程 Collect；weak_ptr 防悬垂）。
class WorldAdminStats : public legend::admin::IServiceProviderStats {
public:
    WorldAdminStats(std::weak_ptr<legend::world::WorldServer> server,
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
        if (auto server = m_server.lock()) {
            s.running = true;
            const auto ws = server->CollectStats();
            s.listenIp = "127.0.0.1";
            s.listenPort = 7200;
            s.connectionCount = ws.playerCount;
            s.requestCount = ws.packetsReceived;
            s.packetsReceived = ws.packetsReceived;
            s.packetsSent = ws.packetsSent;

            // 需求7：World 专属数据
            s.extra.emplace_back("在线玩家", std::to_string(ws.playerCount));
            s.extra.emplace_back("Map1 人数", std::to_string(ws.map1Players));
            s.extra.emplace_back("Map2 人数", std::to_string(ws.map2Players));
            s.extra.emplace_back("Map3 人数", std::to_string(ws.map3Players));
            s.extra.emplace_back("怪物数量", std::to_string(ws.monsterCount));
            s.extra.emplace_back("NPC 数量", std::to_string(ws.npcCount));
            s.extra.emplace_back("掉落数量", std::to_string(ws.dropCount));
            s.extra.emplace_back("传送门数量", std::to_string(ws.portalCount));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.2f ms", ws.tickAvgMs);
            s.extra.emplace_back("性能·Tick平均(ms)", buf);
            std::snprintf(buf, sizeof(buf), "%.2f ms", ws.tickMaxMs);
            s.extra.emplace_back("性能·Tick最大(ms)", buf);
            s.extra.emplace_back("性能·收包数", std::to_string(ws.packetsReceived));
            s.extra.emplace_back("性能·发包数", std::to_string(ws.packetsSent));

            // 需求6/7：依赖服务状态
            const bool charOk = server->IsLoginConnected(); // loginHost 实为 CharacterServer
            s.dependencies.push_back({"CharacterServer", charOk, false,
                                      charOk ? "内部链路已连接" : "连接断开，自动重连中"});
            if (ws.dbDegraded) {
                s.dependencies.push_back({"DbServer", true, true, "Persistence 降级（连续失败，周期重试）"});
            } else {
                s.dependencies.push_back({"DbServer", ws.dbAvailable, false,
                                          ws.dbAvailable ? "PersistenceClient 已连接"
                                                         : "连接断开，自动重连中"});
            }
            if (auto remoteLog = m_remoteLog.lock()) {
                const bool logOk = remoteLog->IsAvailable();
                s.dependencies.push_back({"LogServer", logOk, false,
                                          logOk ? "世界事件上报中" : "连接断开（本地缓冲重发）"});
            } else {
                s.dependencies.push_back({"LogServer", false, false, "未接入"});
            }
        }
        return s;
    }

private:
    std::weak_ptr<legend::world::WorldServer> m_server;
    std::weak_ptr<legend::server::LogClient> m_remoteLog;
    std::chrono::system_clock::time_point m_startTime;
};

int WorldServerMain(const legend::admin::ServerMainParams& params,
                    legend::admin::ServerAdminContext& context, int argc, char** argv) {
    legend::world::WorldServer::Config config;
    std::string configPath = params.configPath;
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
        } else if (arg == "--console" || arg == "--hidden") {
            continue; // Runner 模式开关：业务层无感
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
    if (!params.console) {
        context.AttachLogCapture(); // Stage25.6：GUI 实时日志面板
    }
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
    context.SetStatsProvider(std::make_shared<WorldAdminStats>(world, remoteLog));
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

    LOG_INFO("[World] Shutting down.");
    world->Stop();
    if (remoteLog) remoteLog->Stop();
    service.Stop();
    legend::debug::Logger::Shutdown();
    return 0;
}

} // namespace

// Stage25.6：默认双击 = Windows GUI 管理窗口；--console = 控制台模式（CI/无人值守）。
int main(int argc, char** argv) {
    return legend::admin::RunServerMain(argc, argv, legend::admin::ServerRole::World,
                                        WorldServerMain);
}
