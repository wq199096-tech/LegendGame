#include "Server/WorldServer/WorldServer.h"

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

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            config.listenPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--login-port" && i + 1 < argc) {
            config.loginPort = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "--db" && i + 1 < argc) {
            config.databasePath = argv[++i];
        } else {
            std::printf("Usage: LegendWorldServer.exe [--port 7200] [--login-port 7100] "
                        "[--db data/legend_account.db]\n");
            return arg == "--help" || arg == "-h" ? 0 : 1;
        }
    }

    std::filesystem::create_directories("Logs/World");
    legend::debug::Logger::Init("Logs/World");
    LOG_INFO("[World] listening on 127.0.0.1:" + std::to_string(config.listenPort));

    legend::net::NetworkService service;
    service.Start();

    auto world = std::make_shared<legend::world::WorldServer>(service);
    world->SetHooks({
        .onLoginConnectionChanged =
            [](bool connected) {
                LOG_INFO(connected ? "[World] Login link ready" : "[World] Login link lost");
            },
        .onPlayerChanged =
            [](std::uint64_t characterId, bool entered) {
                if (entered) {
                    LOG_INFO("[World] Player entered character=#" + std::to_string(characterId));
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
    service.Stop();
    legend::debug::Logger::Shutdown();
    return 0;
}
