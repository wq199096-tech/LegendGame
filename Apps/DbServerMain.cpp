#include "Server/Common/ServerConfig.h"
#include "Server/Db/DbServer.h"
#include "Engine/Debug/Logger.h"
#include "Shared/Version.h"

#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

int main(int argc, char** argv) {
    std::string configPath = "Config/servers.json";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) configPath = argv[++i];
        else { std::printf("Usage: LegendDbServer.exe [--config Config/servers.json]\n"); return 1; }
    }
    legend::server::ServerConfig topology; std::string error;
    if (!legend::server::LoadServerConfig(configPath, topology, error)) {
        std::fprintf(stderr, "[Db] config error: %s\n", error.c_str()); return 1;
    }
    const auto* endpoint = legend::server::FindService(topology, "db");
    std::filesystem::create_directories("Logs/Db"); legend::debug::Logger::Init("Logs/Db");
    LOG_INFO("[Db] Service Name=LegendDbServer Version=" + std::string(LEGEND_ENGINE_VERSION) +
             " Protocol=1 Listen=" + endpoint->host + ":" + std::to_string(endpoint->port) +
             " Config=" + configPath + " Dependency Status=Healthy");
    legend::net::NetworkService network; network.Start();
    auto server = std::make_shared<legend::db::DbServer>(network);
    server->GetConfig().listenPort = endpoint->port;
    server->GetConfig().databasePath = topology.databasePath;
    server->GetConfig().serviceToken = topology.sharedSecret;
    if (!server->Start(error)) { LOG_ERROR("[Db] start failed on port " +
        std::to_string(endpoint->port) + ": " + error); network.Stop(); return 1; }
    asio::io_context signals; asio::signal_set set(signals, SIGINT, SIGTERM);
    std::atomic<bool> stop{false}; set.async_wait([&](const std::error_code&, int) { stop.store(true); });
    while (!stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_INFO("[Db] graceful shutdown"); server->Stop(); network.Stop();
    legend::debug::Logger::Shutdown(); return 0;
}
