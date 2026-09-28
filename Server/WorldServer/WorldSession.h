#pragma once

#include "Engine/Network/TcpConnection.h"

#include "Shared/Network/MessageId.h"
#include "Shared/Network/NetworkConstants.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace legend::world {

// 阶段11 指令二十六：WorldSession 状态机（每个 Client 连接一个）。
enum class WorldSessionState {
    Connected,          // TCP 已连，等待 WorldClientHello
    WaitingEnterWorld,  // WorldServerHello 已发，等待 EnterWorldRequest
    ValidatingTicket,   // Ticket 已发 LoginServer 消费
    LoadingCharacter,   // Ticket 消费成功，DB 加载角色中
    InWorld,            // 已进入世界
    Closing,            // 断开中
};

// 阶段11 指令二十六：单个 World Client 连接的会话（不持有业务数据——
// 在线玩家数据在 PlayerSession/WorldManager）。
class WorldSession {
public:
    WorldSession(legend::net::TcpConnectionPtr connection, std::uint64_t connectionId);

    // 返回 false = 协议错误（调用方断开该连接，指令一百零八：只断当前 Client）。
    bool OnPacket(const legend::network::Packet& packet, std::string& error);

    void SendPacket(const legend::network::Packet& packet);
    void Disconnect();

    // 阶段11 指令十九：EnterWorldRequest 一次性取出（io 线程）。
    bool TakePendingEnterWorld(legend::network::Packet& out);

    void SetState(WorldSessionState state) { m_state = state; }
    WorldSessionState State() const { return m_state; }
    std::uint64_t ConnectionId() const { return m_connectionId; }
    std::chrono::steady_clock::time_point LastPacketTime() const { return m_lastPacketTime; }

    // 阶段11 指令十五：身份只能来自 Ticket 消费结果（进入世界后回填，用于日志）。
    void SetIdentity(std::uint64_t accountId, std::uint64_t characterId) {
        m_accountId = accountId;
        m_characterId = characterId;
    }
    std::uint64_t AccountId() const { return m_accountId; }
    std::uint64_t CharacterId() const { return m_characterId; }

private:
    bool HandleHandshake(const legend::network::Packet& packet, std::string& error);
    bool HandlePostHandshake(const legend::network::Packet& packet, std::string& error);

    legend::net::TcpConnectionPtr m_connection;
    std::uint64_t m_connectionId = 0;
    WorldSessionState m_state = WorldSessionState::Connected;
    std::chrono::steady_clock::time_point m_lastPacketTime{std::chrono::steady_clock::now()};
    bool m_hasPendingEnterWorld = false;
    legend::network::Packet m_pendingEnterWorld;
    std::uint64_t m_accountId = 0;
    std::uint64_t m_characterId = 0;
};

} // namespace legend::world
