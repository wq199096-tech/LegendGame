#include "Server/Common/ServerConfig.h"

#include "Shared/InternalProtocol/InternalProtocol.h"

#include <filesystem>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

namespace legend::server {
namespace {

using Json = nlohmann::json;

bool ValidHost(const std::string& host) {
    if (host.empty() || host.size() > 253) {
        return false;
    }
    for (const unsigned char c : host) {
        if (!(std::isalnum(c) || c == '.' || c == '-' || c == ':')) {
            return false;
        }
    }
    return true;
}

} // namespace

ServerConfig MakeDefaultServerConfig() {
    ServerConfig config;
    config.protocolVersion = legend::internal::kInternalProtocolVersion;
    config.services = {
        {"login", {"127.0.0.1", 7100, true}},
        {"world", {"127.0.0.1", 7200, true}},
        {"gateway", {"127.0.0.1", 7300, true}},
        {"character", {"127.0.0.1", 7400, true}},
        {"db", {"127.0.0.1", 7500, true}},
        {"log", {"127.0.0.1", 7600, true}},
    };
    return config;
}

bool LoadServerConfig(const std::string& path, ServerConfig& out, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open server config '" + path + "'";
        return false;
    }
    Json root;
    try {
        input >> root;
        out = MakeDefaultServerConfig();
        out.protocolVersion = root.at("protocolVersion").get<std::uint16_t>();
        const auto& services = root.at("services");
        for (auto it = services.begin(); it != services.end(); ++it) {
            ServiceEndpoint endpoint;
            endpoint.host = it.value().at("host").get<std::string>();
            endpoint.port = it.value().at("port").get<std::uint16_t>();
            endpoint.enabled = it.value().value("enabled", true);
            out.services[it.key()] = std::move(endpoint);
        }
        if (root.contains("persistence")) {
            const auto& persistence = root.at("persistence");
            out.databasePath = persistence.value("databasePath", out.databasePath);
            out.saveIntervalSeconds =
                persistence.value("saveIntervalSeconds", out.saveIntervalSeconds);
        }
        if (root.contains("heartbeat")) {
            const auto& heartbeat = root.at("heartbeat");
            out.heartbeatIntervalSeconds =
                heartbeat.value("intervalSeconds", out.heartbeatIntervalSeconds);
            out.heartbeatTimeoutSeconds =
                heartbeat.value("timeoutSeconds", out.heartbeatTimeoutSeconds);
        }
        if (root.contains("rpc")) {
            out.rpcTimeoutMilliseconds =
                root.at("rpc").value("timeoutMilliseconds", out.rpcTimeoutMilliseconds);
        }
        if (root.contains("security")) {
            out.sharedSecret = root.at("security").value("serviceToken", std::string{});
        }
        if (root.contains("chat")) {
            // Stage27 指令四十六：聊天参数（可覆盖；校验在 ValidateServerConfig）。
            const auto& chat = root.at("chat");
            out.chatNearbyRadius = chat.value("nearbyRadius", out.chatNearbyRadius);
            out.chatMaxCodePoints = chat.value("maxCodepoints", out.chatMaxCodePoints);
            out.chatNearbyWindowMs = chat.value("nearbyWindowMs", out.chatNearbyWindowMs);
            out.chatNearbyMaxPerWindow =
                chat.value("nearbyMaxPerWindow", out.chatNearbyMaxPerWindow);
            out.chatWorldWindowMs = chat.value("worldWindowMs", out.chatWorldWindowMs);
            out.chatWorldMaxPerWindow = chat.value("worldMaxPerWindow", out.chatWorldMaxPerWindow);
            out.chatWhisperWindowMs = chat.value("whisperWindowMs", out.chatWhisperWindowMs);
            out.chatWhisperMaxPerWindow =
                chat.value("whisperMaxPerWindow", out.chatWhisperMaxPerWindow);
            out.chatBurstWindowMs = chat.value("burstWindowMs", out.chatBurstWindowMs);
            out.chatBurstMaxMessages = chat.value("burstMaxMessages", out.chatBurstMaxMessages);
        }
    } catch (const std::exception& ex) {
        error = "invalid server config: " + std::string(ex.what());
        return false;
    }
    return ValidateServerConfig(out, error);
}

bool ValidateServerConfig(const ServerConfig& config, std::string& error) {
    if (config.protocolVersion != legend::internal::kInternalProtocolVersion) {
        error = "internal protocolVersion must be 1";
        return false;
    }
    static const std::set<std::string> required = {
        "login", "character", "gateway", "world", "db", "log"};
    std::set<std::uint16_t> ports;
    for (const auto& name : required) {
        const auto it = config.services.find(name);
        if (it == config.services.end()) {
            error = "missing required service '" + name + "'";
            return false;
        }
        const auto& endpoint = it->second;
        if (!endpoint.enabled) {
            continue;
        }
        if (!ValidHost(endpoint.host) || endpoint.port == 0) {
            error = "invalid endpoint for service '" + name + "'";
            return false;
        }
        if (!ports.insert(endpoint.port).second) {
            error = "duplicate enabled service port " + std::to_string(endpoint.port);
            return false;
        }
    }
    if (config.heartbeatIntervalSeconds == 0 ||
        config.heartbeatTimeoutSeconds <= config.heartbeatIntervalSeconds) {
        error = "heartbeat timeout must be greater than interval";
        return false;
    }
    if (config.saveIntervalSeconds < 30 || config.saveIntervalSeconds > 3600) {
        error = "persistence saveIntervalSeconds must be between 30 and 3600";
        return false;
    }
    if (config.rpcTimeoutMilliseconds < 100 || config.rpcTimeoutMilliseconds > 60000) {
        error = "rpc timeoutMilliseconds must be between 100 and 60000";
        return false;
    }
    // Stage27 指令四十六：聊天参数校验（非法拒绝启动，不静默产生 0 窗口/0 条数）。
    if (!(config.chatNearbyRadius > 0.0f && config.chatNearbyRadius <= 10000.0f)) {
        error = "chat nearbyRadius must be between 0 and 10000";
        return false;
    }
    if (config.chatMaxCodePoints < 1 || config.chatMaxCodePoints > 480) {
        error = "chat maxCodepoints must be between 1 and 480";
        return false;
    }
    const auto validRate = [](int windowMs, int maxPerWindow) {
        return windowMs >= 100 && windowMs <= 60000 && maxPerWindow >= 1 && maxPerWindow <= 60;
    };
    if (!validRate(config.chatNearbyWindowMs, config.chatNearbyMaxPerWindow) ||
        !validRate(config.chatWorldWindowMs, config.chatWorldMaxPerWindow) ||
        !validRate(config.chatWhisperWindowMs, config.chatWhisperMaxPerWindow) ||
        !validRate(config.chatBurstWindowMs, config.chatBurstMaxMessages)) {
        error = "chat rate windows must be 100-60000 ms and maxPerWindow 1-60";
        return false;
    }
    return true;
}

const ServiceEndpoint* FindService(const ServerConfig& config, const std::string& name) {
    const auto it = config.services.find(name);
    return it == config.services.end() ? nullptr : &it->second;
}

} // namespace legend::server
