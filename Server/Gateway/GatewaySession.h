#pragma once

#include "Engine/Network/TcpConnection.h"

#include "Shared/Network/MessageId.h"
#include "Shared/Network/NetworkConstants.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace legend::gateway {

// 阶段9 指令七十五：Gateway Client Session 状态机。
enum class GatewaySessionState {
    Connected,          // TCP 已连，等待 ClientHello
    HandshakeCompleted, // ServerHello 已发
    LoginPending,       // LoginRequest 已转发 LoginServer
    Authenticated,      // 登录成功
    Closing,
};

// 阶段9 指令四十七/七十五~八十：单个 Client 连接的会话。
// 不持有业务指针（指令五十七）；token 不落日志也不长期保存（指令五十二）。
class GatewaySession {
public:
    GatewaySession(legend::net::TcpConnectionPtr connection, std::uint64_t connectionId);

    // 返回 false 表示协议错误（调用方应断开该连接：指令六十九/七十）
    bool OnPacket(const legend::network::Packet& packet, std::string& error);

    void SendPacket(const legend::network::Packet& packet);
    void Disconnect();

    // 登录结果回填（GatewayServer 从 LoginServer 收到 LoginGatewayResponse 后调用）
    void CompleteLogin(bool success, std::uint32_t accountId, const std::string& displayName,
                       const std::string& message);

    // 一次性取出待转发登录数据（GatewayServer 检测到 LoginPending 后调用；
    // token 只在内存暂存，不落日志——指令五十二）
    bool TakePendingLogin(std::string& username, std::string& token);

    GatewaySessionState State() const { return m_state; }
    std::uint64_t ConnectionId() const { return m_connectionId; }
    bool IsAuthenticated() const { return m_state == GatewaySessionState::Authenticated; }
    const std::string& Username() const { return m_username; }
    std::chrono::steady_clock::time_point LastPacketTime() const { return m_lastPacketTime; }

    // 取出待转发登录数据（一次性消费；token 不落日志——指令五十二）
    bool TakePendingForward(std::string& username, std::string& token);

private:
    bool HandleHandshake(const legend::network::Packet& packet, std::string& error);
    bool HandlePostHandshake(const legend::network::Packet& packet, std::string& error);
    void SendLoginError(const std::string& message);

    legend::net::TcpConnectionPtr m_connection;
    std::uint64_t m_connectionId = 0;
    GatewaySessionState m_state = GatewaySessionState::Connected;
    std::string m_username;
    std::string m_pendingUsername; // 暂存待转发（token 不落日志，指令五十二）
    std::string m_pendingToken;
    bool m_hasPendingLogin = false;
    std::uint32_t m_lastPingSequence = 0;
    std::chrono::steady_clock::time_point m_lastPacketTime{std::chrono::steady_clock::now()};
};

} // namespace legend::gateway
