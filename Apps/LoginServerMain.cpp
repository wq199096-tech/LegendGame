#include "Server/LoginServer/LoginServer.h"

#include "Engine/Debug/Logger.h"

#include <filesystem>
#include <string>

// LegendLoginServer：纯 Console 认证服务（阶段9 指令五十三/八十六/一百二十三）。
int main(int argc, char** argv) {
    legend::login::LoginServer::Config config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            config.listenPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else {
            std::printf("Usage: LegendLoginServer.exe [--port 7100]\n");
            return arg == "--help" || arg == "-h" ? 0 : 1;
        }
    }

    std::filesystem::create_directories("Logs/Login");
    legend::debug::Logger::Init("Logs/Login");
    LOG_INFO("[Login] Listening on 127.0.0.1:" + std::to_string(config.listenPort));

    legend::net::NetworkService service;
    service.Start();

    auto login = std::make_shared<legend::login::LoginServer>(service);
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
            [](std::uint16_t innerMessageId, std::uint64_t accountId, bool success) {
                LOG_INFO(std::string("[Login] Account result msg=") +
                         legend::network::MessageIdName(innerMessageId) + " account=" +
                         std::to_string(accountId) + " -> " + (success ? "success" : "failed"));
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
    service.Stop();
    legend::debug::Logger::Shutdown();
    return 0;
}
