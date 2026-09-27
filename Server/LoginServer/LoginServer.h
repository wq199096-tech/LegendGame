#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpServer.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace legend::network {
struct Packet;
}

namespace legend::login {

// 阶段9 指令五十三/九十一：LoginServer——纯 Console（不依赖 SDL/OpenGL，
// 指令八十六）。临时认证：username=="test" && token=="dev_token" -> success。
class LoginServer : public std::enable_shared_from_this<LoginServer> {
public:
    struct Hooks {
        std::function<void(bool connected)> onGatewayConnectionChanged;
        std::function<void(std::uint64_t requestId, const std::string& username, bool success,
                           std::uint32_t accountId)>
            onAuthResult;
    };

    explicit LoginServer(legend::net::NetworkService& service);

    struct Config {
        std::uint16_t listenPort = 7100; // 指令四十一：默认 127.0.0.1:7100
    };

    bool Start(std::string& error);
    void Stop();
    Config& GetConfig() { return m_config; } // 测试可改端口（Start 前设置）
    std::size_t GatewayCount() const { return m_gateways.size(); }
    void SetHooks(Hooks hooks) { m_hooks = std::move(hooks); }

private:
    struct GatewayLink {
        legend::net::TcpConnectionPtr connection;
        bool handshakeDone = false;
    };

    void OnGatewayAccepted(legend::net::TcpConnectionPtr connection);
    void OnGatewayPacket(std::uint64_t connectionId, const legend::network::Packet& packet);
    void OnGatewayClosed(std::uint64_t connectionId);
    void HandleAuthRequest(std::uint64_t gatewayConnectionId,
                           const legend::network::Packet& packet);

    legend::net::NetworkService& m_service;
    Config m_config;
    legend::net::TcpServer m_server;
    std::map<std::uint64_t, GatewayLink> m_gateways;
    std::atomic<bool> m_stopped{false};
    Hooks m_hooks;
};

} // namespace legend::login
