#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace legend::server {

struct ServiceEndpoint {
    std::string host = "127.0.0.1";
    std::uint16_t port = 0;
    bool enabled = true;
};

struct ServerConfig {
    std::uint16_t protocolVersion = 1;
    std::map<std::string, ServiceEndpoint> services;
    std::string databasePath = "Data/legend_account.db";
    std::string sharedSecret;
    std::uint32_t saveIntervalSeconds = 60;
    std::uint32_t heartbeatIntervalSeconds = 5;
    std::uint32_t heartbeatTimeoutSeconds = 15;
    std::uint32_t rpcTimeoutMilliseconds = 5000;
};

ServerConfig MakeDefaultServerConfig();
bool LoadServerConfig(const std::string& path, ServerConfig& out, std::string& error);
bool ValidateServerConfig(const ServerConfig& config, std::string& error);
const ServiceEndpoint* FindService(const ServerConfig& config, const std::string& name);

} // namespace legend::server
