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
    // Stage27 指令四十六：聊天参数（Config/servers.json "chat" 节；缺省 = 代码默认）。
    float chatNearbyRadius = 1200.0f;   // 附近聊天半径（world units）
    int chatMaxCodePoints = 120;        // 消息码点上限
    int chatNearbyWindowMs = 1000;      // 附近窗口
    int chatNearbyMaxPerWindow = 2;     // 附近 1 秒最多 2 条
    int chatWorldWindowMs = 3000;       // 世界窗口
    int chatWorldMaxPerWindow = 1;      // 世界 3 秒最多 1 条
    int chatWhisperWindowMs = 1000;     // 私聊窗口
    int chatWhisperMaxPerWindow = 3;    // 私聊 1 秒最多 3 条
    int chatBurstWindowMs = 10000;      // 跨频道突发窗口
    int chatBurstMaxMessages = 8;       // 10 秒最多 8 条
};

ServerConfig MakeDefaultServerConfig();
bool LoadServerConfig(const std::string& path, ServerConfig& out, std::string& error);
bool ValidateServerConfig(const ServerConfig& config, std::string& error);
const ServiceEndpoint* FindService(const ServerConfig& config, const std::string& name);

} // namespace legend::server
