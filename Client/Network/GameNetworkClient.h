#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpClient.h"

#include "Shared/Network/MessageId.h"
#include "Shared/Network/NetworkConstants.h"
#include "Shared/Network/Protocol.h" // 阶段9.2指令一：Shared Protocol 统一

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace legend::client {

// 阶段9 指令二十八：Client 连接状态机。
enum class NetworkState {
    Disconnected,
    Connecting,
    Connected,
    Handshaking,
    Ready,
    Failed,
};

// 阶段9 指令三十二：NetworkEvent（network thread -> 主线程事件队列）。
struct NetworkEvent {
    enum class Type {
        Connected,
        ConnectFailed,
        Disconnected,
        HandshakeSuccess,
        HandshakeFailed,
        LoginResponse,
        HeartbeatTimeout,
        ProtocolError, // 阶段9.2指令十：未知 MessageId
    };
    Type type = Type::Disconnected;
    std::string message;
    bool loginSuccess = false;
    std::uint64_t accountId = 0;              // 阶段9.2指令四：全链 uint64
    std::uint16_t errorCode = 0;              // 阶段9.2指令五：LoginErrorCode 全链
    std::string displayName;
    float rttMs = -1.0f;
};

// 阶段9 指令三十一/三十三：GameNetworkClient——网络线程收包只 Push 事件，
// 绝不跨线程操作游戏对象；主线程 PollEvents 消费。
// 指令六十二~六十六：Ready 后每 5s HeartbeatPing，15s 无 Pong 判超时断开。
class GameNetworkClient : public std::enable_shared_from_this<GameNetworkClient> {
public:
    // 指令四十二：默认连接配置集中于此（不散落多个 cpp）
    struct Config {
        std::string gatewayHost = "127.0.0.1";
        std::uint16_t gatewayPort = 7000;
        std::string clientBuild = "0.9.0";
        std::string clientName = "LegendClient";
        double heartbeatIntervalSeconds = 5.0;
        double heartbeatTimeoutSeconds = 15.0;
    };

    GameNetworkClient();
    ~GameNetworkClient();

    void Connect(const std::string& host, std::uint16_t port); // 异步，不阻塞主循环
    void Disconnect(bool notifyServer); // 指令六十七：ClientQuit 发 DisconnectNotice
    void SendLogin(const std::string& username, const std::string& token);

    void PollEvents(std::deque<NetworkEvent>& out); // 主线程消费
    void UpdateHeartbeat(float deltaTime);          // 主线程：超时检测

    NetworkState State() const { return m_state.load(); }
    std::uint64_t ServerConnectionId() const { return m_serverConnectionId.load(); }
    bool IsAuthenticated() const { return m_authenticated.load(); }
    float LastRttMs() const { return m_lastRttMs.load(); }
    std::string LastError() const;
    const Config& GetConfig() const { return m_config; }

private:
    void OnTransportConnected(std::shared_ptr<legend::net::TcpConnection> connection);
    void OnPacket(const legend::network::Packet& packet);
    void OnConnectionClosed(std::uint64_t id, const std::error_code& ec);
    void SendClientHello();
    void SendHeartbeat();
    void ScheduleHeartbeat();
    void ScheduleHeartbeatImpl();
    void PushEvent(NetworkEvent event);
    void SetState(NetworkState state, const std::string& error = {});

    legend::net::NetworkService m_service;
    std::shared_ptr<legend::net::TcpClient> m_client;
    std::shared_ptr<legend::net::TcpConnection> m_connection;
    Config m_config;

    std::atomic<NetworkState> m_state{NetworkState::Disconnected};
    std::atomic<bool> m_authenticated{false};
    std::atomic<std::uint64_t> m_serverConnectionId{0};
    std::atomic<float> m_lastRttMs{-1.0f};

    std::mutex m_eventMutex;
    std::deque<NetworkEvent> m_events;
    mutable std::mutex m_errorMutex; // const LastError() 也要加锁
    std::string m_lastError;

    asio::steady_timer m_heartbeatTimer;
    std::chrono::steady_clock::time_point m_lastPongTime{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point m_lastPingTime{};
    std::uint32_t m_pingSequence = 0;
    std::atomic<bool> m_heartbeatScheduled{false};
};

} // namespace legend::client
