#pragma once

#include "Engine/Network/TcpConnection.h"

#include "Shared/Network/MessageId.h"
#include "Shared/Network/NetworkConstants.h"
#include "Shared/Network/Protocol.h" // 阶段9.1：LoginErrorCode/payload Encode/Decode

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>

namespace legend::gateway {

// 阶段9 指令七十五：Gateway Client Session 状态机。
enum class GatewaySessionState {
    Connected,          // TCP 已连，等待 ClientHello
    HandshakeCompleted, // ServerHello 已发
    LoginPending,       // LoginRequest 已转发 LoginServer
    Authenticated,      // 登录成功
    CharacterSelected, // 角色归属已由 CharacterServer 确认
    InWorld,           // WorldServer 已确认进入世界
    Closing,
};

// 阶段9 指令四十七/七十五~八十：单个 Client 连接的会话。
// 不持有业务指针（指令五十七）；token 不落日志也不长期保存（指令五十二）。
// 阶段10 指令五十四：Account 消息只暂存转发（Gateway 不解析业务 payload）。
class GatewaySession {
public:
    GatewaySession(legend::net::TcpConnectionPtr connection, std::uint64_t connectionId);

    // 返回 false 表示协议错误（调用方应断开该连接：指令六十九/七十）
    bool OnPacket(const legend::network::Packet& packet, std::string& error);

    void SendPacket(const legend::network::Packet& packet);
    void Disconnect();

    // 登录结果回填（阶段9.1指令七：只更新状态，不发送——GatewayServer 统一单发）
    void CompleteLogin(bool success, std::uint64_t accountId, const std::string& displayName,
                       const std::string& message);
    void MarkAuthenticated(std::uint64_t accountId);
    void MarkCharacterSelected(std::uint64_t characterId);
    void MarkInWorld();
    bool CanRoute(std::uint16_t messageId) const;

    // 一次性取出待转发登录数据（GatewayServer 检测到 LoginPending 后调用；
    // token 只在内存暂存，不落日志——指令五十二）
    bool TakePendingLogin(std::string& username, std::string& token);

    // 阶段10 指令一百零二：写请求 in-flight 去重。
    // 返回 false = 同类请求已在途（调用方应回 RequestPending 错误响应）。
    bool TryBeginAccount(std::uint16_t innerMessageId, std::uint64_t requestId);
    void EndAccount(std::uint16_t innerMessageId);
    // 连接断开时清理全部 in-flight（响应到达时自然丢弃）。
    void CancelAllAccount();
    bool HasInflightAccount() const;

    // 一次性取出待转发的 Account 包（GatewayServer 包装信封后转发）。
    bool TakePendingAccountForward(legend::network::Packet& out);

    GatewaySessionState State() const { return m_state; }
    void Touch() { m_lastPacketTime = std::chrono::steady_clock::now(); }
    std::uint64_t ConnectionId() const { return m_connectionId; }
    bool IsAuthenticated() const {
        return m_state == GatewaySessionState::Authenticated ||
               m_state == GatewaySessionState::CharacterSelected ||
               m_state == GatewaySessionState::InWorld;
    }
    std::uint64_t AccountId() const { return m_accountId; }
    std::uint64_t CharacterId() const { return m_characterId; }
    const std::string& Username() const { return m_username; }
    std::chrono::steady_clock::time_point LastPacketTime() const { return m_lastPacketTime; }

    // 取出待转发登录数据（一次性消费；token 不落日志——指令五十二）
    bool TakePendingForward(std::string& username, std::string& token);

private:
    bool HandleHandshake(const legend::network::Packet& packet, std::string& error);
    bool HandlePostHandshake(const legend::network::Packet& packet, std::string& error);
    void SendLoginError(legend::network::LoginErrorCode errorCode, const std::string& message);

    legend::net::TcpConnectionPtr m_connection;
    std::uint64_t m_connectionId = 0;
    // Stage25.6：atomic——管理台 GUI 线程 CollectStats 与 io 线程并发读写同一状态
    std::atomic<GatewaySessionState> m_state{GatewaySessionState::Connected};
    std::string m_username;
    std::string m_pendingUsername; // 暂存待转发（token 不落日志，指令五十二）
    std::string m_pendingToken;
    bool m_hasPendingLogin = false;
    std::uint64_t m_accountId = 0;  // 阶段9.1指令十四：accountId 统一 uint64
    std::uint64_t m_characterId = 0;
    std::string m_displayName;
    std::uint32_t m_lastPingSequence = 0;
    std::chrono::steady_clock::time_point m_lastPacketTime{std::chrono::steady_clock::now()};

    // 阶段10：待转发 Account 包（io 线程内访问）与 in-flight 写请求表
    bool m_hasPendingAccountForward = false;
    legend::network::Packet m_pendingAccountPacket;
    std::map<std::uint16_t, std::uint64_t> m_inflightAccount; // innerMessageId -> requestId
};

} // namespace legend::gateway
