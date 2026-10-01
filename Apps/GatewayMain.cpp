#include "Server/Gateway/GatewayServer.h"
#include "Server/Common/ServerConfig.h"
#include "Server/AdminUi/ServerMainRunner.h"

#include "Engine/Debug/Logger.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

namespace {

using legend::gateway::GatewayServer;

// Stage25.6：Gateway 管理台统计适配器（GUI 线程 Collect；weak_ptr 防悬垂）。
class GatewayAdminStats : public legend::admin::IServiceProviderStats {
public:
    GatewayAdminStats(std::weak_ptr<GatewayServer> server, std::string listenIp,
                      std::uint16_t listenPort, std::uint16_t worldPort)
        : m_server(std::move(server)),
          m_listenIp(std::move(listenIp)),
          m_listenPort(listenPort),
          m_worldPort(worldPort),
          m_startTime(std::chrono::system_clock::now()) {}

    legend::admin::ServiceSnapshot Collect() override {
        legend::admin::ServiceSnapshot s;
        s.listenIp = m_listenIp;
        s.listenPort = m_listenPort;
        s.startTime = m_startTime;
        s.uptimeSeconds =
            std::chrono::duration<double>(std::chrono::system_clock::now() - m_startTime)
                .count();
        if (auto server = m_server.lock()) {
            s.running = true;
            const auto gw = server->CollectStats();
            s.connectionCount = gw.clientCount;
            s.requestCount = gw.packetsReceived;
            s.packetsReceived = gw.packetsReceived;
            s.packetsSent = gw.packetsSent;

            // 需求7：Gateway 分层状态计数（连接页显示）
            s.extra.emplace_back("会话状态·已连接(待握手)", std::to_string(gw.stateConnected));
            s.extra.emplace_back("会话状态·握手完成", std::to_string(gw.stateHandshakeCompleted));
            s.extra.emplace_back("会话状态·登录转发中", std::to_string(gw.stateLoginPending));
            s.extra.emplace_back("会话状态·Authenticated", std::to_string(gw.stateAuthenticated));
            s.extra.emplace_back("会话状态·CharacterSelected",
                                 std::to_string(gw.stateCharacterSelected));
            s.extra.emplace_back("会话状态·InWorld", std::to_string(gw.stateInWorld));
            s.extra.emplace_back("会话状态·Closing", std::to_string(gw.stateClosing));
            s.extra.emplace_back("登录转发送入(in-flight)", std::to_string(gw.pendingLoginCount));
            s.extra.emplace_back("World 代理连接数", std::to_string(gw.worldProxyCount));

            // 性能页
            s.extra.emplace_back("性能·收包数", std::to_string(gw.packetsReceived));
            s.extra.emplace_back("性能·发包数", std::to_string(gw.packetsSent));

            // 需求6/7：依赖服务状态
            const bool loginOk = server->IsLoginConnected();
            s.dependencies.push_back({"LoginServer", loginOk, false,
                                      loginOk ? "内部链路已连接" : "连接断开，自动重连中"});
            const bool charOk = server->IsCharacterConnected();
            s.dependencies.push_back({"CharacterServer", charOk, false,
                                      charOk ? "内部链路已连接" : "连接断开，自动重连中"});
            s.dependencies.push_back({"WorldServer", m_worldPort != 0, false,
                                      m_worldPort != 0 ? "按需代理（进入世界时建立）"
                                                       : "未配置专用 World 通道"});
            s.dependencies.push_back({"LogServer", false, false, "未接入（Gateway 不上报远程日志）"});
        }
        return s;
    }

private:
    std::weak_ptr<GatewayServer> m_server;
    std::string m_listenIp;
    std::uint16_t m_listenPort;
    std::uint16_t m_worldPort;
    std::chrono::system_clock::time_point m_startTime;
};

int GatewayServerMain(const legend::admin::ServerMainParams& params,
                      legend::admin::ServerAdminContext& context, int argc, char** argv) {
    legend::gateway::GatewayConfig config;
    std::string listenIp = "127.0.0.1";
    std::uint16_t worldPort = 0;
    std::string configPath = params.configPath;
    legend::server::ServerConfig topology;
    std::string configError;
    if (legend::server::LoadServerConfig(configPath, topology, configError)) {
        const auto* gateway = legend::server::FindService(topology, "gateway");
        const auto* login = legend::server::FindService(topology, "login");
        const auto* character = legend::server::FindService(topology, "character");
        const auto* world = legend::server::FindService(topology, "world");
        listenIp = gateway->host;
        config.listenPort = gateway->port;
        config.loginHost = login->host; config.loginPort = login->port;
        config.characterHost = character->host; config.characterPort = character->port;
        config.worldHost = world->host; config.worldPort = world->port;
        worldPort = world->port;
    }

    // 命令行参数（未知参数打印 usage；--config/--console/--hidden 由 Runner 预扫描，此处跳过）
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            return (i + 1 < argc) ? argv[++i] : std::string();
        };
        if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
            if (!legend::server::LoadServerConfig(configPath, topology, configError)) {
                std::fprintf(stderr, "Config error: %s\n", configError.c_str()); return 1;
            }
            const auto* gateway = legend::server::FindService(topology, "gateway");
            const auto* login = legend::server::FindService(topology, "login");
            const auto* character = legend::server::FindService(topology, "character");
            const auto* world = legend::server::FindService(topology, "world");
            listenIp = gateway->host;
            config.listenPort=gateway->port; config.loginHost=login->host; config.loginPort=login->port;
            config.characterHost=character->host; config.characterPort=character->port;
            config.worldHost=world->host; config.worldPort=world->port;
            worldPort = world->port;
        } else if (arg == "--console" || arg == "--hidden") {
            continue; // Runner 模式开关：业务层无感
        } else if (arg == "--port" && i + 1 < argc) {
            config.listenPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--login-host" && i + 1 < argc) {
            config.loginHost = argv[++i];
        } else if (arg == "--login-port" && i + 1 < argc) {
            config.loginPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else {
            std::printf("Usage: LegendGateway.exe [--config Config/servers.json] [--port 7300] "
                        "[--login-host 127.0.0.1] [--login-port 7100]\n");
            return arg == "--help" || arg == "-h" ? 0 : 1;
        }
    }

    std::filesystem::create_directories("Logs/Gateway");
    legend::debug::Logger::Init("Logs/Gateway");
    if (!params.console) {
        context.AttachLogCapture(); // Stage25.6：GUI 实时日志面板
    }
    LOG_INFO("[Gateway] Listening on 127.0.0.1:" + std::to_string(config.listenPort));

    legend::net::NetworkService service;
    service.Start();

    auto gateway = std::make_shared<legend::gateway::GatewayServer>(service, config);
    context.SetStatsProvider(std::make_shared<GatewayAdminStats>(
        gateway, listenIp, config.listenPort, worldPort));
    gateway->SetHooks({
        .onClientHandshakeComplete =
            [](std::uint64_t connectionId) {
                LOG_INFO("[Gateway] Client #" + std::to_string(connectionId) +
                         " handshake complete.");
            },
        .onClientClosed =
            [](std::uint64_t connectionId, const std::string& reason) {
                LOG_INFO("[Gateway] Client #" + std::to_string(connectionId) + " closed: " +
                         reason);
            },
        .onLoginConnectionChanged =
            [](bool connected) {
                LOG_INFO(connected ? "[Gateway] Login server connected."
                                   : "[Gateway] Login server unavailable (will retry).");
            },
    });

    std::string error;
    if (!gateway->Start(error)) {
        LOG_ERROR("[Gateway] Failed to listen: " + error);
        service.Stop();
        legend::debug::Logger::Shutdown();
        return 1;
    }

    // Ctrl+C（--console）与 GUI 关窗（externalStop）统一触发 Graceful Stop
    asio::io_context signals;
    asio::signal_set set(signals, SIGINT, SIGTERM);
    std::atomic<bool> stopRequested{false};
    set.async_wait([&](const std::error_code&, int) { stopRequested.store(true); });

    // 主线程等待（不 busy poll——条件变量 + 100ms 节拍即可，阶段9 无游戏逻辑 Tick）
    while (!stopRequested.load() &&
           !(params.externalStop && params.externalStop->load())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    set.cancel();
    signals.stop();

    LOG_INFO("[Gateway] Shutting down.");
    gateway->Stop();
    service.Stop();
    legend::debug::Logger::Shutdown();
    return 0;
}

} // namespace

// Stage25.6：默认双击 = Windows GUI 管理窗口；--console = 控制台模式（CI/无人值守）。
int main(int argc, char** argv) {
    return legend::admin::RunServerMain(argc, argv, legend::admin::ServerRole::Gateway,
                                        GatewayServerMain);
}
