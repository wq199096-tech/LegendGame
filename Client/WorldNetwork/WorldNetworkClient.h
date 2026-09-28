#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpClient.h"

#include "Shared/Combat/CombatProtocol.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/Monster/MonsterProtocol.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/Network/MessageId.h"
#include "Shared/Network/NetworkConstants.h"
#include "Shared/World/WorldProtocol.h"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace legend::client {

// 阶段11 指令四十四：WorldFlowState（客户端世界流程状态机）。
enum class WorldFlowState {
    Disconnected,
    Connecting,
    Handshaking,
    WaitingEnterWorld,
    EnteringWorld,
    WorldReady,
    Failed,
};

const char* WorldFlowStateName(WorldFlowState state);

// 阶段11 指令四十九：WorldNetworkEvent（world 网络线程 -> 主线程事件队列）。
struct WorldNetworkEvent {
    enum class Type {
        Connected,
        ConnectFailed,
        Disconnected,
        HandshakeSuccess,
        HandshakeFailed,
        EnterWorldSuccess,
        EnterWorldFailed,
        PositionSnapshot,
        ProtocolError,
        // 阶段12 指令四十六：AOI 多玩家同步事件
        PlayerSpawn,
        PlayerDespawn,
        RemotePlayerBatchSnapshot,
        // 阶段13 指令五十九：服务器权威怪物事件
        MonsterSpawn,
        MonsterDespawn,
        MonsterBatchSnapshot,
        // 阶段14 指令六十二：服务器权威战斗事件
        AttackResponse,
        CombatEvent,
        HealthSnapshot,
        MonsterDeath,
        PlayerDeath,
    };
    Type type = Type::Disconnected;
    std::string message;

    // EnterWorldResponse / PlayerSpawn 字段（指令二十/二十二）
    std::uint64_t requestId = 0;
    std::uint64_t accountId = 0;
    std::uint64_t characterId = 0;
    std::string characterName;
    std::uint16_t classId = 0;
    std::uint16_t gender = 0;
    std::uint32_t level = 1;
    std::uint16_t mapId = 1;
    float positionX = 0.0f;
    float positionY = 0.0f;
    std::uint16_t errorCode = 0;

    // PlayerPositionSnapshot 字段（指令四十二）
    std::uint32_t lastProcessedInputSequence = 0;

    // 阶段12：PlayerDespawn reason（指令二十三）+ batch 快照（指令二十五）
    std::uint8_t despawnReason = 0;
    std::uint64_t serverTime = 0;
    std::vector<world::RemotePlayerBatchEntry> batchPlayers;

    // 阶段13：Monster 事件字段（指令五十九；Spawn 元数据复用 name/level/mapId/x/y）
    std::uint64_t monsterEntityId = 0;
    std::uint32_t monsterTypeId = 0;
    std::uint8_t monsterState = 0;
    std::vector<world::MonsterSnapshotEntry> monsterBatch;

    // 阶段14：Combat 事件字段（指令六十二；EnterWorldResponse/PlayerSpawn/
    // MonsterSpawn 复用 currentHp/maxHp/alive）
    bool success = false;           // AttackResponse
    std::uint8_t resultCode = 0;    // AttackResponse（CombatResultCode）
    std::uint64_t targetEntityId = 0; // AttackResponse
    std::uint64_t eventId = 0;      // CombatEvent
    std::uint8_t attackerType = 0;  // CombatEvent / PlayerDeath(killerType)
    std::uint64_t attackerId = 0;   // CombatEvent / PlayerDeath(killerId)
    std::uint8_t targetType = 0;    // CombatEvent
    std::uint64_t targetId = 0;     // CombatEvent
    std::uint32_t damage = 0;       // CombatEvent
    std::uint32_t targetHpAfter = 0; // CombatEvent
    std::uint32_t targetMaxHp = 0;  // CombatEvent
    bool killed = false;            // CombatEvent
    std::uint8_t entityType = 0;    // HealthSnapshot（CombatEntityType）
    std::uint64_t entityId = 0;     // HealthSnapshot
    std::uint32_t currentHp = 0;    // HealthSnapshot
    std::uint32_t maxHp = 0;        // HealthSnapshot
    bool alive = true;              // HealthSnapshot
};

// 阶段11 指令四十五/四十七/四十八/七十七/七十八：
// WorldNetworkClient —— 独立的世界连接（直连 127.0.0.1:7200）。
// - 异步连接，失败不阻塞游戏（状态 Failed）
// - network thread -> event queue -> main thread（禁止跨线程改游戏对象）
// - 独立 Heartbeat 状态（5s Ping / 15s timeout），不复用 GameNetworkClient
class WorldNetworkClient : public std::enable_shared_from_this<WorldNetworkClient> {
public:
    struct Config {
        std::string worldHost = "127.0.0.1";
        std::uint16_t worldPort = 7200;
        std::string clientBuild = "0.11.0";
        std::string clientName = "LegendClient";
        double heartbeatIntervalSeconds = 5.0;
        double heartbeatTimeoutSeconds = 15.0;
    };

    WorldNetworkClient();
    ~WorldNetworkClient();

    void Connect(); // 异步（端点经 SetWorldEndpoint 配置）
    void Disconnect(bool notifyServer); // 指令七十九：发 WorldDisconnectNotice

    // 阶段11 指令三：世界端点配置（默认 127.0.0.1:7200）。
    void SetWorldEndpoint(const std::string& host, std::uint16_t port) {
        m_config.worldHost = host;
        m_config.worldPort = port;
    }

    void SendEnterWorld(const std::string& selectionTicket);
    void SendMoveInput(std::uint32_t inputSequence, float directionX, float directionY,
                       float deltaTime);
    // 阶段14 指令五十九/六十一：Debug 攻击——只发"我想攻击谁"
    //（禁止传坐标/hitbox/damage，指令三；服务器重新验证，指令六十）。
    void SendAttack(std::uint64_t requestId, std::uint8_t targetEntityType,
                    std::uint64_t targetEntityId);

    void PollEvents(std::deque<WorldNetworkEvent>& out); // 主线程消费
    void UpdateHeartbeat(float deltaTime);

    WorldFlowState State() const { return m_state.load(); }
    std::uint64_t ServerConnectionId() const { return m_serverConnectionId.load(); }
    float LastRttMs() const { return m_lastRttMs.load(); }
    std::string LastError() const;
    const Config& GetConfig() const { return m_config; }

private:
    void OnTransportConnected(std::shared_ptr<legend::net::TcpConnection> connection);
    void OnPacket(const legend::network::Packet& packet);
    void SendWorldHello();
    void SendHeartbeat();
    void ScheduleHeartbeat();
    void PushEvent(WorldNetworkEvent event);
    void SetState(WorldFlowState state, const std::string& error = {});

    legend::net::NetworkService m_service;
    std::shared_ptr<legend::net::TcpClient> m_client;
    std::shared_ptr<legend::net::TcpConnection> m_connection;
    Config m_config;

    std::atomic<WorldFlowState> m_state{WorldFlowState::Disconnected};
    std::atomic<std::uint64_t> m_serverConnectionId{0};
    std::atomic<float> m_lastRttMs{-1.0f};

    std::mutex m_eventMutex;
    std::deque<WorldNetworkEvent> m_events;
    mutable std::mutex m_errorMutex;
    std::string m_lastError;

    asio::steady_timer m_heartbeatTimer;
    std::chrono::steady_clock::time_point m_lastPongTime{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point m_lastPingTime{};
    std::uint32_t m_pingSequence = 0;
    std::atomic<bool> m_heartbeatScheduled{false};
    std::string m_pendingTicket; // 握手成功后自动发送 EnterWorld（指令四十六）
};

} // namespace legend::client
