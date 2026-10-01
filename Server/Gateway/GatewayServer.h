#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpClient.h"
#include "Engine/Network/TcpServer.h"
#include "Server/Gateway/GatewaySession.h"
#include "Shared/Account/AccountError.h"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace legend::gateway {

// 阶段9 指令四十七~六十一：GatewayServer——只做 握手/心跳/Login转发/生命周期。
struct GatewayConfig {
    std::uint16_t listenPort = 7000;        // 指令四十：默认 127.0.0.1:7000
    std::string loginHost = "127.0.0.1";
    std::uint16_t loginPort = 7100;         // 指令四十一
    std::string characterHost = "127.0.0.1";
    std::uint16_t characterPort = 0; // 0 keeps legacy LoginServer character routing for tests
    std::string worldHost = "127.0.0.1";
    std::uint16_t worldPort = 0; // 0 disables the dedicated world-channel proxy in legacy tests
    double loginReconnectSeconds = 2.0;     // 指令五十：服务间重连节奏（测试可调短）
    double pendingLoginTimeoutSeconds = 4.0; // 指令六十：3~5 秒无响应超时（测试可调短）
    double clientIdleTimeoutSeconds = 20.0; // 阶段9.1指令三十七/三十八：正式 20s（测试 0.5s）
};

// 阶段9.1指令十五：登录结果错误码透传（hooks 用 uint64 accountId，指令十四）
struct LoginResult {
    bool success = false;
    std::uint64_t accountId = 0;
    std::uint16_t errorCode = 0;
    std::string message;
};

class GatewayServer : public std::enable_shared_from_this<GatewayServer> {
public:
    // 测试钩子（阶段9 验收用；正式运行时可空）
    struct Hooks {
        std::function<void(std::uint64_t connectionId)> onClientHandshakeComplete;
        std::function<void(std::uint64_t connectionId, bool success, std::uint64_t accountId,
                           const std::string& displayName)>
            onLoginResult;
        std::function<void(std::uint64_t connectionId, const std::string& reason)> onClientClosed;
        std::function<void(bool connected)> onLoginConnectionChanged;
    };

    GatewayServer(legend::net::NetworkService& service, const GatewayConfig& config);

    bool Start(std::string& error);
    void Stop();

    std::size_t ClientCount() const;
    // 阶段9.1指令三十四：测试只读统计（DisconnectCleanup 验证 Pending 清空）
    std::size_t PendingLoginCount() const {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
        return m_pendingLogins.size();
    }
    bool IsLoginConnected() const { return m_loginAvailable.load(); }
    bool IsCharacterConnected() const { return m_characterAvailable.load(); }
    void SetHooks(Hooks hooks) { m_hooks = std::move(hooks); }

private:
    struct PendingLogin {
        std::uint64_t requestId = 0;
        std::uint64_t clientConnectionId = 0;
        std::chrono::steady_clock::time_point createdAt{std::chrono::steady_clock::now()};
    };

    // 阶段10：Account 请求 pending（信封转发后等待 LoginServer 响应）
    struct PendingAccount {
        std::uint64_t clientConnectionId = 0;
        std::uint16_t innerMessageId = 0;
        std::chrono::steady_clock::time_point createdAt{std::chrono::steady_clock::now()};
    };

    void OnClientAccepted(legend::net::TcpConnectionPtr connection);
    void OnClientPacket(std::uint64_t connectionId, const legend::network::Packet& packet);
    void OnClientClosed(std::uint64_t connectionId, const std::error_code& ec);

    void ConnectToLogin();
    void ScheduleLoginReconnect();
    void OnLoginConnected(legend::net::TcpConnectionPtr connection);
    void HandleLoginLinkClosed(); // 阶段10：Login 链路关闭统一入口（self 保活）
    void OnLoginPacket(std::uint64_t linkId, const legend::network::Packet& packet);
    void HandleLoginGatewayResponse(const legend::network::Packet& packet);
    void ConnectToCharacter();
    void ScheduleCharacterReconnect();
    void OnCharacterConnected(legend::net::TcpConnectionPtr connection);
    void HandleCharacterLinkClosed();
    void OnCharacterPacket(std::uint64_t linkId, const legend::network::Packet& packet);
    void StartWorldProxy(std::uint64_t clientConnectionId,
                         const legend::network::Packet& firstPacket,
                         const std::shared_ptr<GatewaySession>& session);
    void OnWorldProxyConnected(std::uint64_t clientConnectionId,
                               legend::net::TcpConnectionPtr connection);
    void OnWorldProxyPacket(std::uint64_t clientConnectionId,
                            const legend::network::Packet& packet);
    void CloseWorldProxy(std::uint64_t clientConnectionId);

    // 阶段10：Account 信封转发 / 响应回送 / 超时合成错误响应
    void ForwardAccountPacket(std::uint64_t clientConnectionId,
                              const legend::network::Packet& packet);
    void HandleAccountResponse(const legend::network::Packet& packet);
    void SendAccountErrorResponse(std::uint64_t clientConnectionId, std::uint64_t requestId,
                                  std::uint16_t innerMessageId,
                                  legend::account::AccountErrorCode errorCode,
                                  const std::string& message);

    void ForwardLogin(std::uint64_t clientConnectionId, const std::string& username,
                      const std::string& token);
    void SendToClient(std::uint64_t connectionId, const legend::network::Packet& packet);
    void SendLoginError(std::uint64_t connectionId, legend::network::LoginErrorCode errorCode,
                        const std::string& message);
    void CheckPendingTimeouts();
    void SchedulePendingTimeoutCheck();
    void CheckIdleTimeouts();
    void ScheduleIdleTimeoutCheck();

    legend::net::NetworkService& m_service;
    GatewayConfig m_config;
    std::shared_ptr<legend::net::TcpServer> m_server;
    std::shared_ptr<legend::net::TcpClient> m_loginClient;
    legend::net::TcpConnectionPtr m_loginConnection;
    std::atomic<bool> m_loginAvailable{false};
    bool m_loginHandshakeDone = false; // 指令九：Gateway→Login 内部握手（io 线程内访问）
    std::shared_ptr<legend::net::TcpClient> m_characterClient;
    legend::net::TcpConnectionPtr m_characterConnection;
    std::atomic<bool> m_characterAvailable{false};
    bool m_characterHandshakeDone = false;
    std::atomic<bool> m_stopped{false};
    std::atomic<bool> m_reconnectScheduled{false};
    std::atomic<bool> m_characterReconnectScheduled{false};

    std::map<std::uint64_t, std::shared_ptr<GatewaySession>> m_sessions; // connectionId -> session
    std::map<std::uint64_t, PendingLogin> m_pendingLogins;               // requestId -> pending
    std::map<std::uint64_t, PendingAccount> m_pendingAccounts;           // requestId -> pending
    struct WorldProxy {
        std::shared_ptr<legend::net::TcpClient> connector;
        legend::net::TcpConnectionPtr backend;
        std::shared_ptr<GatewaySession> client;
        std::vector<legend::network::Packet> pending;
    };
    std::map<std::uint64_t, WorldProxy> m_worldProxies;
    // 阶段10：三张表由 io 线程（收包/关闭/超时回调）与主线程（Stop clear）并发
    // 访问——加锁（数据竞态修复；io 侧单线程，锁仅用于与 Stop 的互斥）
    mutable std::mutex m_mapsMutex;
    std::uint64_t m_nextRequestId = 1; // 指令五十八：单调增长

    asio::steady_timer m_reconnectTimer;
    asio::steady_timer m_characterReconnectTimer;
    asio::steady_timer m_pendingTimer;
    asio::steady_timer m_idleTimer; // Client idle scan (idle

    Hooks m_hooks;
};

} // namespace legend::gateway
