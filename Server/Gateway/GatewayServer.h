#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpClient.h"
#include "Engine/Network/TcpServer.h"
#include "Server/Gateway/GatewaySession.h"

#include <asio.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace legend::gateway {

// 阶段9 指令四十七~六十一：GatewayServer——只做 握手/心跳/Login转发/生命周期。
struct GatewayConfig {
    std::uint16_t listenPort = 7000;        // 指令四十：默认 127.0.0.1:7000
    std::string loginHost = "127.0.0.1";
    std::uint16_t loginPort = 7100;         // 指令四十一
    double loginReconnectSeconds = 2.0;     // 指令五十：服务间重连节奏（测试可调短）
    double pendingLoginTimeoutSeconds = 4.0; // 指令六十：3~5 秒无响应超时（测试可调短）
};

class GatewayServer : public std::enable_shared_from_this<GatewayServer> {
public:
    // 测试钩子（阶段9 验收用；正式运行时可空）
    struct Hooks {
        std::function<void(std::uint64_t connectionId)> onClientHandshakeComplete;
        std::function<void(std::uint64_t connectionId, bool success, std::uint32_t accountId,
                           const std::string& displayName)>
            onLoginResult;
        std::function<void(std::uint64_t connectionId, const std::string& reason)> onClientClosed;
        std::function<void(bool connected)> onLoginConnectionChanged;
    };

    GatewayServer(legend::net::NetworkService& service, const GatewayConfig& config);

    bool Start(std::string& error);
    void Stop();

    std::size_t ClientCount() const;
    bool IsLoginConnected() const { return m_loginAvailable.load(); }
    void SetHooks(Hooks hooks) { m_hooks = std::move(hooks); }

private:
    struct PendingLogin {
        std::uint64_t requestId = 0;
        std::uint64_t clientConnectionId = 0;
        std::chrono::steady_clock::time_point createdAt{std::chrono::steady_clock::now()};
    };

    void OnClientAccepted(legend::net::TcpConnectionPtr connection);
    void OnClientPacket(std::uint64_t connectionId, const legend::network::Packet& packet);
    void OnClientClosed(std::uint64_t connectionId, const std::error_code& ec);

    void ConnectToLogin();
    void ScheduleLoginReconnect();
    void OnLoginConnected(legend::net::TcpConnectionPtr connection);
    void OnLoginClosed(const std::error_code& ec);
    void OnLoginPacket(const legend::network::Packet& packet);
    void HandleLoginGatewayResponse(const legend::network::Packet& packet);

    void ForwardLogin(std::uint64_t clientConnectionId, const std::string& username,
                      const std::string& token);
    void SendToClient(std::uint64_t connectionId, const legend::network::Packet& packet);
    void SendLoginError(std::uint64_t connectionId, const std::string& message);
    void CheckPendingTimeouts();
    void SchedulePendingTimeoutCheck();

    legend::net::NetworkService& m_service;
    GatewayConfig m_config;
    legend::net::TcpServer m_server;
    std::shared_ptr<legend::net::TcpClient> m_loginClient;
    legend::net::TcpConnectionPtr m_loginConnection;
    std::atomic<bool> m_loginAvailable{false};
    std::atomic<bool> m_stopped{false};
    std::atomic<bool> m_reconnectScheduled{false};

    std::map<std::uint64_t, std::shared_ptr<GatewaySession>> m_sessions; // connectionId -> session
    std::map<std::uint64_t, PendingLogin> m_pendingLogins;               // requestId -> pending
    std::uint64_t m_nextRequestId = 1; // 指令五十八：单调增长

    asio::steady_timer m_reconnectTimer;
    asio::steady_timer m_pendingTimer;

    Hooks m_hooks;
};

} // namespace legend::gateway
