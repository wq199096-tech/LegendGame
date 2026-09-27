#include "Server/Gateway/GatewayServer.h"

#include "Engine/Debug/Logger.h"

#include <cstring>
#include <filesystem>
#include <string>

// LegendGateway：纯 Console 网关（阶段9 指令四十七/八十六/一百二十三/一百二十四）。
// 不 link SDL/OpenGL/Renderer/GameScene。
int main(int argc, char** argv) {
    legend::gateway::GatewayConfig config;

    // 指令一百二十四：命令行参数（未知参数打印 usage）
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            return (i + 1 < argc) ? argv[++i] : std::string();
        };
        if (arg == "--port" && i + 1 < argc) {
            config.listenPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--login-host" && i + 1 < argc) {
            config.loginHost = argv[++i];
        } else if (arg == "--login-port" && i + 1 < argc) {
            config.loginPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else {
            std::printf("Usage: LegendGateway.exe [--port 7000] [--login-host 127.0.0.1] "
                        "[--login-port 7100]\n");
            return arg == "--help" || arg == "-h" ? 0 : 1;
        }
    }

    std::filesystem::create_directories("Logs/Gateway");
    legend::debug::Logger::Init("Logs/Gateway");
    LOG_INFO("[Gateway] Listening on 127.0.0.1:" + std::to_string(config.listenPort));

    legend::net::NetworkService service;
    service.Start();

    auto gateway = std::make_shared<legend::gateway::GatewayServer>(service, config);
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

    // 指令一百二十三：Ctrl+C 触发 Graceful Stop（asio signal_set；Windows 支持）
    asio::io_context signals;
    asio::signal_set set(signals, SIGINT, SIGTERM);
    std::atomic<bool> stopRequested{false};
    set.async_wait([&](const std::error_code&, int) { stopRequested.store(true); });

    // 主线程等待（不 busy poll——条件变量 + 100ms 节拍即可，阶段9 无游戏逻辑 Tick）
    while (!stopRequested.load()) {
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
