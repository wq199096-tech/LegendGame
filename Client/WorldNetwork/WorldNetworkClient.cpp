#include "Client/WorldNetwork/WorldNetworkClient.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/Protocol.h"
#include "Shared/Progression/ProgressionProtocol.h"

namespace legend::client {

using legend::network::ClientHelloPayload;
using legend::network::DecodeHeartbeatPong;
using legend::network::DecodeServerHello;
using legend::network::EncodeClientHello;
using legend::network::EncodeHeartbeatPing;
using legend::network::HeartbeatPingPayload;
using legend::network::HeartbeatPongPayload;
using legend::network::MessageId;
using legend::network::Packet;
using legend::network::ServerHelloPayload;
namespace net = legend::net;
namespace world = legend::world;

const char* WorldFlowStateName(WorldFlowState state) {
    switch (state) {
        case WorldFlowState::Disconnected: return "Disconnected";
        case WorldFlowState::Connecting: return "Connecting";
        case WorldFlowState::Handshaking: return "Handshaking";
        case WorldFlowState::WaitingEnterWorld: return "WaitingEnterWorld";
        case WorldFlowState::EnteringWorld: return "EnteringWorld";
        case WorldFlowState::WorldReady: return "WorldReady";
        case WorldFlowState::Failed: return "Failed";
    }
    return "Unknown";
}

WorldNetworkClient::WorldNetworkClient()
    : m_client(std::make_shared<net::TcpClient>(m_service)),
      m_heartbeatTimer(m_service.Io()) {}

WorldNetworkClient::~WorldNetworkClient() {
    // 析构：同步关闭（保证 FIN 落地），再停 io（异步 Close 可能被丢）。
    if (m_connection) {
        m_connection->CloseBlocking();
        m_connection.reset();
    } else {
        Disconnect(false);
    }
    m_service.Stop();
}

void WorldNetworkClient::Connect() {
    const WorldFlowState state = m_state.load();
    if (state == WorldFlowState::Connecting || state == WorldFlowState::Handshaking ||
        state == WorldFlowState::WaitingEnterWorld || state == WorldFlowState::EnteringWorld ||
        state == WorldFlowState::WorldReady) {
        return;
    }
    m_service.Start();
    SetState(WorldFlowState::Connecting);
    auto self = shared_from_this();
    m_client->Connect(
        m_config.worldHost, m_config.worldPort,
        [self](net::TcpConnectionPtr connection) {
            self->OnTransportConnected(std::move(connection));
        },
        [self](const std::error_code& ec) {
            // 指令四十七：连接失败不阻塞游戏，状态 Failed
            self->SetState(WorldFlowState::Failed, ec ? ec.message() : "connect failed");
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::ConnectFailed;
            event.message = ec ? ec.message() : "connect failed";
            self->PushEvent(std::move(event));
        });
}

void WorldNetworkClient::Disconnect(bool notifyServer) {
    if (notifyServer && m_connection && m_connection->IsConnected()) {
        // 阶段11 指令七十九：客户端退出世界发 WorldDisconnectNotice
        world::WorldDisconnectNoticePayload notice;
        notice.reason = "ClientQuit";
        Packet out;
        out.header.messageId = static_cast<std::uint16_t>(MessageId::WorldDisconnectNotice);
        if (world::EncodeWorldDisconnectNotice(notice, out.payload)) {
            m_connection->Send(out);
        }
    }
    m_heartbeatTimer.cancel();
    if (m_connection) {
        m_connection->Close();
        m_connection.reset();
    }
    SetState(WorldFlowState::Disconnected);
}

void WorldNetworkClient::SendEnterWorld(const std::string& selectionTicket) {
    if (m_state.load() != WorldFlowState::WaitingEnterWorld || !m_connection) {
        m_pendingTicket = selectionTicket; // 握手完成前先暂存（指令四十六）
        return;
    }
    world::EnterWorldRequestPayload request;
    request.requestId = 1;
    request.selectionTicket = selectionTicket;
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::EnterWorldRequest);
    if (world::EncodeEnterWorldRequest(request, out.payload)) {
        SetState(WorldFlowState::EnteringWorld);
        m_connection->Send(out);
    }
}

void WorldNetworkClient::SendMoveInput(std::uint32_t inputSequence, float directionX,
                                       float directionY, float deltaTime) {
    if (m_state.load() != WorldFlowState::WorldReady || !m_connection) {
        return;
    }
    world::PlayerMoveInputPayload input;
    input.inputSequence = inputSequence;
    input.directionX = directionX;
    input.directionY = directionY;
    input.deltaTime = deltaTime;
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::PlayerMoveInput);
    if (world::EncodePlayerMoveInput(input, out.payload)) {
        m_connection->Send(out);
    }
}

void WorldNetworkClient::SendAttack(std::uint64_t requestId, std::uint8_t targetEntityType,
                                    std::uint64_t targetEntityId) {
    // 阶段14 指令五十九/六十一：Client 只发目标（requestId/type/id），
    // 不发攻击起点/终点/hitbox/damage（指令三）。
    if (m_state.load() != WorldFlowState::WorldReady || !m_connection) {
        return;
    }
    world::PlayerAttackRequestPayload request;
    request.requestId = requestId;
    request.targetEntityType = targetEntityType;
    request.targetEntityId = targetEntityId;
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::PlayerAttackRequest);
    if (world::EncodePlayerAttackRequest(request, out.payload)) {
        m_connection->Send(out);
    }
}

void WorldNetworkClient::SendSkillCast(std::uint64_t requestId, std::uint32_t skillId,
                                       std::uint8_t targetType, std::uint64_t targetEntityId) {
    // 阶段15 指令二十二/二十三：Client 只发 skillId + 目标（requestId/type/id），
    // 不发伤害/Mana/CD/CastTime/AOE 位置/命中结果（指令二十三/九十六/九十七）。
    if (m_state.load() != WorldFlowState::WorldReady || !m_connection) {
        return;
    }
    world::SkillCastRequestPayload request;
    request.requestId = requestId;
    request.skillId = skillId;
    request.targetType = targetType;
    request.targetEntityId = targetEntityId;
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::SkillCastRequest);
    if (world::EncodeSkillCastRequest(request, out.payload)) {
        m_connection->Send(out);
    }
}

// ---------------------------------------------------------------------------
// 阶段18：拾取/装备/卸下请求（Client 只发 id/槽位，数值全部服务器权威）
// ---------------------------------------------------------------------------

void WorldNetworkClient::SendItemPickup(std::uint64_t requestId, std::uint64_t dropEntityId) {
    if (m_state.load() != WorldFlowState::WorldReady || !m_connection) {
        return;
    }
    world::ItemPickupRequestPayload request;
    request.requestId = requestId;
    request.dropEntityId = dropEntityId;
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::ItemPickupRequest);
    if (world::EncodeItemPickupRequest(request, out.payload)) {
        m_connection->Send(out);
    }
}

void WorldNetworkClient::SendEquipItem(std::uint64_t requestId, std::uint32_t slotIndex) {
    if (m_state.load() != WorldFlowState::WorldReady || !m_connection) {
        return;
    }
    world::EquipItemRequestPayload request;
    request.requestId = requestId;
    request.slotIndex = slotIndex;
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::EquipItemRequest);
    if (world::EncodeEquipItemRequest(request, out.payload)) {
        m_connection->Send(out);
    }
}

void WorldNetworkClient::SendUnequipItem(std::uint64_t requestId, std::uint8_t equipmentSlot) {
    if (m_state.load() != WorldFlowState::WorldReady || !m_connection) {
        return;
    }
    world::UnequipItemRequestPayload request;
    request.requestId = requestId;
    request.equipmentSlot = equipmentSlot;
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::UnequipItemRequest);
    if (world::EncodeUnequipItemRequest(request, out.payload)) {
        m_connection->Send(out);
    }
}

void WorldNetworkClient::PollEvents(std::deque<WorldNetworkEvent>& out) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    while (!m_events.empty()) {
        out.push_back(std::move(m_events.front()));
        m_events.pop_front();
    }
}

void WorldNetworkClient::UpdateHeartbeat(float) {
    // 阶段11 指令七十七：15 秒无 Pong -> 断开（独立于 GameNetworkClient 的心跳状态）
    if (m_state.load() != WorldFlowState::WorldReady || !m_connection) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - m_lastPongTime).count();
    if (elapsed > m_config.heartbeatTimeoutSeconds) {
        SetState(WorldFlowState::Disconnected, "heartbeat timeout");
        WorldNetworkEvent event;
        event.type = WorldNetworkEvent::Type::Disconnected;
        event.message = "no heartbeat pong";
        PushEvent(std::move(event));
        Disconnect(false);
    }
}

void WorldNetworkClient::OnTransportConnected(net::TcpConnectionPtr connection) {
    m_connection = connection;
    SetState(WorldFlowState::Handshaking);
    connection->Start(
        [self = shared_from_this()](const Packet& packet) { self->OnPacket(packet); },
        [self = shared_from_this()](std::uint64_t, const std::error_code& ec) {
            self->m_heartbeatTimer.cancel();
            self->SetState(WorldFlowState::Disconnected, ec ? ec.message() : "connection closed");
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::Disconnected;
            event.message = ec ? ec.message() : "connection closed";
            self->PushEvent(std::move(event));
        });
    WorldNetworkEvent event;
    event.type = WorldNetworkEvent::Type::Connected;
    PushEvent(std::move(event));
    SendWorldHello();
}

void WorldNetworkClient::SendWorldHello() {
    if (!m_connection) {
        return;
    }
    // 阶段11 指令十七：WorldClientHello 与 ClientHello 同布局
    ClientHelloPayload hello;
    hello.protocolVersion = world::kWorldProtocolVersion;
    hello.clientBuild = m_config.clientBuild;
    hello.clientName = m_config.clientName;
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::WorldClientHello);
    if (EncodeClientHello(hello, out.payload)) {
        m_connection->Send(out);
    }
}

void WorldNetworkClient::OnPacket(const Packet& packet) {
    switch (static_cast<MessageId>(packet.header.messageId)) {
        case MessageId::WorldServerHello: {
            // 阶段11 指令十八：WorldServerHello 与 ServerHello 同布局
            ServerHelloPayload hello;
            std::string decodeError;
            if (!DecodeServerHello(packet.payload.data(), packet.payload.size(), hello,
                                   decodeError)) {
                SetState(WorldFlowState::Failed, "malformed WorldServerHello");
                WorldNetworkEvent event;
                event.type = WorldNetworkEvent::Type::HandshakeFailed;
                event.message = "malformed WorldServerHello";
                PushEvent(std::move(event));
                return;
            }
            if (!hello.accepted) {
                SetState(WorldFlowState::Failed, hello.message);
                WorldNetworkEvent event;
                event.type = WorldNetworkEvent::Type::HandshakeFailed;
                event.message = hello.message;
                PushEvent(std::move(event));
                Disconnect(false);
                return;
            }
            m_serverConnectionId.store(hello.connectionId);
            m_lastPongTime = std::chrono::steady_clock::now();
            SetState(WorldFlowState::WaitingEnterWorld);
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::HandshakeSuccess;
            event.message = hello.serverName;
            PushEvent(std::move(event));
            // 阶段11 指令四十六：握手成功即发送 EnterWorld（ticket 已暂存）
            if (!m_pendingTicket.empty()) {
                SendEnterWorld(m_pendingTicket);
                m_pendingTicket.clear();
            }
            ScheduleHeartbeat();
            return;
        }
        case MessageId::EnterWorldResponse: {
            world::EnterWorldResponsePayload response;
            std::string decodeError;
            if (!world::DecodeEnterWorldResponse(packet.payload.data(), packet.payload.size(),
                                                 response, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            if (response.success) {
                SetState(WorldFlowState::WorldReady);
                event.type = WorldNetworkEvent::Type::EnterWorldSuccess;
            } else {
                SetState(WorldFlowState::Failed, response.message);
                event.type = WorldNetworkEvent::Type::EnterWorldFailed;
            }
            event.requestId = response.requestId;
            event.accountId = response.accountId;
            event.characterId = response.characterId;
            event.characterName = response.characterName;
            event.classId = response.classId;
            event.gender = response.gender;
            event.level = response.level;
            event.mapId = response.mapId;
            event.positionX = response.positionX;
            event.positionY = response.positionY;
            // 阶段14 指令十七：进入世界返回玩家 HP
            event.currentHp = response.currentHp;
            event.maxHp = response.maxHp;
            event.alive = response.alive;
            // 阶段15 指令六十九：进入世界返回玩家 Mana
            event.currentMana = response.currentMana;
            event.maxManaVal = response.maxMana;
            event.errorCode = response.errorCode;
            event.message = response.message;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::PlayerPositionSnapshot: {
            world::PlayerPositionSnapshotPayload snapshot;
            std::string decodeError;
            if (!world::DecodePlayerPositionSnapshot(packet.payload.data(), packet.payload.size(),
                                                     snapshot, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::PositionSnapshot;
            event.characterId = snapshot.characterId;
            event.positionX = snapshot.positionX;
            event.positionY = snapshot.positionY;
            event.lastProcessedInputSequence = snapshot.lastProcessedInputSequence;
            event.serverTime = snapshot.serverTime;
            PushEvent(std::move(event));
            return;
        }
        // ------------------------------------------------------------------
        // 阶段12 指令四十六：AOI 多玩家同步（Spawn/Despawn/Remote batch）
        // ------------------------------------------------------------------
        case MessageId::PlayerSpawn: {
            world::PlayerSpawnPayload spawn;
            std::string decodeError;
            if (!world::DecodePlayerSpawn(packet.payload.data(), packet.payload.size(), spawn,
                                          decodeError)) {
                return; // 畸形包丢弃（服务器只断自己连接的语义由服务端负责）
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::PlayerSpawn;
            event.characterId = spawn.characterId;
            event.characterName = spawn.name;
            event.classId = spawn.classId;
            event.gender = spawn.gender;
            event.level = spawn.level;
            event.mapId = spawn.mapId;
            event.positionX = spawn.positionX;
            event.positionY = spawn.positionY;
            event.serverTime = spawn.serverTime;
            // 阶段14 指令十六：PlayerSpawn 携带 HP
            event.currentHp = spawn.currentHp;
            event.maxHp = spawn.maxHp;
            event.alive = spawn.alive;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::PlayerDespawn: {
            world::PlayerDespawnPayload despawn;
            std::string decodeError;
            if (!world::DecodePlayerDespawn(packet.payload.data(), packet.payload.size(), despawn,
                                            decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::PlayerDespawn;
            event.characterId = despawn.characterId;
            event.despawnReason = despawn.reason;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::RemotePlayerSnapshot: {
            // 阶段12 服务器只发 batch；单条(232)转成单元素 batch 事件统一处理。
            world::RemotePlayerSnapshotPayload snapshot;
            std::string decodeError;
            if (!world::DecodeRemotePlayerSnapshot(packet.payload.data(), packet.payload.size(),
                                                   snapshot, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::RemotePlayerBatchSnapshot;
            event.serverTime = snapshot.serverTime;
            event.batchPlayers.push_back({snapshot.characterId, snapshot.positionX,
                                          snapshot.positionY,
                                          snapshot.lastProcessedInputSequence});
            PushEvent(std::move(event));
            return;
        }
        case MessageId::RemotePlayerBatchSnapshot: {
            world::RemotePlayerBatchSnapshotPayload batch;
            std::string decodeError;
            if (!world::DecodeRemotePlayerBatchSnapshot(packet.payload.data(),
                                                        packet.payload.size(), batch,
                                                        decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::RemotePlayerBatchSnapshot;
            event.serverTime = batch.serverTime;
            event.batchPlayers = std::move(batch.players);
            PushEvent(std::move(event));
            return;
        }
        // ------------------------------------------------------------------
        // 阶段13 指令五十九：Monster 事件（Spawn/Despawn/Batch）
        // ------------------------------------------------------------------
        case MessageId::MonsterSpawn: {
            world::MonsterSpawnPayload spawn;
            std::string decodeError;
            if (!world::DecodeMonsterSpawn(packet.payload.data(), packet.payload.size(), spawn,
                                           decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::MonsterSpawn;
            event.monsterEntityId = spawn.entityId;
            event.monsterTypeId = spawn.monsterTypeId;
            event.monsterState = spawn.state;
            event.characterName = spawn.name;
            event.level = spawn.level;
            event.mapId = spawn.mapId;
            event.positionX = spawn.positionX;
            event.positionY = spawn.positionY;
            event.serverTime = spawn.serverTime;
            // 阶段14 指令十五：MonsterSpawn 携带 HP
            event.currentHp = spawn.currentHp;
            event.maxHp = spawn.maxHp;
            event.alive = spawn.alive;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::MonsterDespawn: {
            world::MonsterDespawnPayload despawn;
            std::string decodeError;
            if (!world::DecodeMonsterDespawn(packet.payload.data(), packet.payload.size(), despawn,
                                             decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::MonsterDespawn;
            event.monsterEntityId = despawn.entityId;
            event.despawnReason = despawn.reason;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::MonsterBatchSnapshot: {
            world::MonsterBatchSnapshotPayload batch;
            std::string decodeError;
            if (!world::DecodeMonsterBatchSnapshot(packet.payload.data(), packet.payload.size(),
                                                   batch, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::MonsterBatchSnapshot;
            event.serverTime = batch.serverTime;
            event.monsterBatch = std::move(batch.monsters);
            PushEvent(std::move(event));
            return;
        }
        // ------------------------------------------------------------------
        // 阶段14 指令六十二：服务器权威战斗事件（AttackResponse/CombatEvent/
        // HealthSnapshot/MonsterDeath/PlayerDeath）
        // ------------------------------------------------------------------
        case MessageId::PlayerAttackResponse: {
            world::PlayerAttackResponsePayload response;
            std::string decodeError;
            if (!world::DecodePlayerAttackResponse(packet.payload.data(), packet.payload.size(),
                                                   response, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::AttackResponse;
            event.requestId = response.requestId;
            event.success = response.success;
            event.resultCode = response.resultCode;
            event.targetEntityId = response.targetEntityId;
            event.message = response.message;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::CombatEvent: {
            world::CombatEventPayload payload;
            std::string decodeError;
            if (!world::DecodeCombatEvent(packet.payload.data(), packet.payload.size(), payload,
                                          decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::CombatEvent;
            event.eventId = payload.eventId;
            event.attackerType = payload.attackerType;
            event.attackerId = payload.attackerId;
            event.targetType = payload.targetType;
            event.targetId = payload.targetId;
            event.damage = payload.damage;
            event.targetHpAfter = payload.targetHpAfter;
            event.targetMaxHp = payload.targetMaxHp;
            event.killed = payload.killed;
            event.serverTime = payload.serverTime;
            // 阶段15 指令三十二：伤害来源（BasicAttack/Skill + skillId）。
            event.sourceType = payload.sourceType;
            event.sourceId = payload.sourceId;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::EntityHealthSnapshot: {
            world::EntityHealthSnapshotPayload payload;
            std::string decodeError;
            if (!world::DecodeEntityHealthSnapshot(packet.payload.data(), packet.payload.size(),
                                                   payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::HealthSnapshot;
            event.entityType = payload.entityType;
            event.entityId = payload.entityId;
            event.currentHp = payload.currentHp;
            event.maxHp = payload.maxHp;
            event.alive = payload.alive;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::MonsterDeath: {
            world::MonsterDeathPayload payload;
            std::string decodeError;
            if (!world::DecodeMonsterDeath(packet.payload.data(), packet.payload.size(), payload,
                                           decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::MonsterDeath;
            event.monsterEntityId = payload.entityId;
            event.characterId = payload.killerCharacterId;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::PlayerDeath: {
            world::PlayerDeathPayload payload;
            std::string decodeError;
            if (!world::DecodePlayerDeath(packet.payload.data(), packet.payload.size(), payload,
                                          decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::PlayerDeath;
            event.characterId = payload.characterId;
            event.attackerType = payload.killerType;
            event.attackerId = payload.killerId;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        // ------------------------------------------------------------------
        // 阶段15 指令五十五：服务器权威技能事件（Client 不做本地伤害预测，
        // 指令五十八：进度只展示，完成必须等 Completed/Impact）
        // ------------------------------------------------------------------
        case MessageId::SkillCastResponse: {
            world::SkillCastResponsePayload payload;
            std::string decodeError;
            if (!world::DecodeSkillCastResponse(packet.payload.data(), packet.payload.size(),
                                                payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::SkillCastResponseEvent;
            event.requestId = payload.requestId;
            event.skillId = payload.skillId;
            event.accepted = payload.accepted;
            event.skillResultCode = payload.resultCode;
            event.currentMana = payload.currentMana;
            event.message = payload.message;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::SkillCastStarted: {
            world::SkillCastStartedPayload payload;
            std::string decodeError;
            if (!world::DecodeSkillCastStarted(packet.payload.data(), packet.payload.size(),
                                               payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::SkillCastStartedEvent;
            event.castId = payload.castId;
            event.characterId = payload.casterCharacterId;
            event.skillId = payload.skillId;
            event.skillTargetType = payload.targetType;
            event.targetEntityId = payload.targetEntityId;
            event.castTimeMs = payload.castTimeMs;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::SkillCastCompleted: {
            world::SkillCastCompletedPayload payload;
            std::string decodeError;
            if (!world::DecodeSkillCastCompleted(packet.payload.data(), packet.payload.size(),
                                                 payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::SkillCastCompletedEvent;
            event.castId = payload.castId;
            event.characterId = payload.casterCharacterId;
            event.skillId = payload.skillId;
            event.skillTargetType = payload.targetType;
            event.targetEntityId = payload.targetEntityId;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::SkillCastCancelled: {
            world::SkillCastCancelledPayload payload;
            std::string decodeError;
            if (!world::DecodeSkillCastCancelled(packet.payload.data(), packet.payload.size(),
                                                 payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::SkillCastCancelledEvent;
            event.castId = payload.castId;
            event.characterId = payload.casterCharacterId;
            event.skillId = payload.skillId;
            event.cancelReason = payload.reason;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::SkillImpactEvent: {
            world::SkillImpactEventPayload payload;
            std::string decodeError;
            if (!world::DecodeSkillImpactEvent(packet.payload.data(), packet.payload.size(),
                                               payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::SkillImpact;
            event.castId = payload.castId;
            event.skillId = payload.skillId;
            event.characterId = payload.casterCharacterId;
            event.serverTime = payload.serverTime;
            event.impactTargets = payload.targets;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::ManaSnapshot: {
            world::ManaSnapshotPayload payload;
            std::string decodeError;
            if (!world::DecodeManaSnapshot(packet.payload.data(), packet.payload.size(), payload,
                                           decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::ManaSnapshot;
            event.currentMana = payload.currentMana;
            event.maxManaVal = payload.maxMana;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        // ------------------------------------------------------------------
        // 阶段16 指令五十二~五十五：服务器权威状态事件（Client 只展示）
        // ------------------------------------------------------------------
        case MessageId::StatusEffectApplied: {
            world::StatusEffectAppliedPayload payload;
            std::string decodeError;
            if (!world::DecodeStatusEffectApplied(packet.payload.data(), packet.payload.size(),
                                                  payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::StatusAppliedEvent;
            event.status.instanceId = payload.instanceId;
            event.status.effectId = payload.effectId;
            event.status.targetType = payload.targetType;
            event.status.targetEntityId = payload.targetEntityId;
            event.status.sourceType = payload.sourceType;
            event.status.sourceEntityId = payload.sourceEntityId;
            event.status.sourceSkillId = payload.sourceSkillId;
            event.status.stacks = payload.stacks;
            event.status.durationMs = payload.durationMs;
            event.status.remainingMs = payload.remainingMs;
            event.status.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::StatusEffectUpdated: {
            world::StatusEffectUpdatedPayload payload;
            std::string decodeError;
            if (!world::DecodeStatusEffectUpdated(packet.payload.data(), packet.payload.size(),
                                                  payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::StatusUpdatedEvent;
            event.status.instanceId = payload.instanceId;
            event.status.effectId = payload.effectId;
            event.status.targetType = payload.targetType;
            event.status.targetEntityId = payload.targetEntityId;
            event.status.stacks = payload.stacks;
            event.status.remainingMs = payload.remainingMs;
            event.status.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::StatusEffectRemoved: {
            world::StatusEffectRemovedPayload payload;
            std::string decodeError;
            if (!world::DecodeStatusEffectRemoved(packet.payload.data(), packet.payload.size(),
                                                  payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::StatusRemovedEvent;
            event.status.instanceId = payload.instanceId;
            event.status.effectId = payload.effectId;
            event.status.targetType = payload.targetType;
            event.status.targetEntityId = payload.targetEntityId;
            event.status.reason = payload.reason;
            event.status.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::StatusEffectSnapshot: {
            world::StatusEffectSnapshotPayload payload;
            std::string decodeError;
            if (!world::DecodeStatusEffectSnapshot(packet.payload.data(), packet.payload.size(),
                                                   payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::StatusSnapshotEvent;
            event.status.targetType = payload.targetType;
            event.status.targetEntityId = payload.targetEntityId;
            event.status.serverTime = payload.serverTime;
            event.status.snapshotEffects = payload.effects;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::RewardGranted: {
            world::RewardGrantedPayload payload;
            std::string decodeError;
            if (!world::DecodeRewardGranted(packet.payload.data(), packet.payload.size(),
                                            payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::RewardGrantedEvent;
            event.characterId = payload.characterId;
            event.progression.sourceMonsterEntityId = payload.sourceMonsterEntityId;
            event.progression.expGranted = payload.expGranted;
            event.progression.goldGranted = payload.goldGranted;
            event.progression.newExperience = payload.newExperience;
            event.progression.newGold = payload.newGold;
            event.progression.level = payload.level;
            event.progression.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::LevelUpEvent: {
            world::LevelUpEventPayload payload;
            std::string decodeError;
            if (!world::DecodeLevelUpEvent(packet.payload.data(), packet.payload.size(),
                                           payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::LevelUpEvent;
            event.characterId = payload.characterId;
            event.progression.oldLevel = payload.oldLevel;
            event.progression.newLevel = payload.newLevel;
            event.progression.currentExp = payload.currentExp;
            event.progression.expToNext = payload.nextLevelExp;
            event.progression.newMaxHp = payload.newMaxHp;
            event.progression.newAttackPower = payload.newAttackPower;
            event.progression.newDefense = payload.newDefense;
            event.progression.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::ProgressionSnapshot: {
            world::ProgressionSnapshotPayload payload;
            std::string decodeError;
            if (!world::DecodeProgressionSnapshot(packet.payload.data(), packet.payload.size(),
                                                  payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::ProgressionSnapshotEvent;
            event.characterId = payload.characterId;
            event.progression.level = payload.level;
            event.progression.currentExp = payload.experience;
            event.progression.expToNext = payload.expToNext;
            event.progression.newGold = payload.gold;
            event.progression.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        // ------------------------------------------------------------------
        // 阶段18：掉落/背包/装备（Client 只做镜像）
        // ------------------------------------------------------------------
        case MessageId::WorldItemSpawn: {
            world::WorldItemSpawnPayload payload;
            std::string decodeError;
            if (!world::DecodeWorldItemSpawn(packet.payload.data(), packet.payload.size(),
                                            payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::WorldItemSpawnEvent;
            event.dropEntityId = payload.dropEntityId;
            event.itemDefinitionId = payload.itemDefinitionId;
            event.itemQuantity = payload.quantity;
            event.mapId = payload.mapId;
            event.positionX = payload.x;
            event.positionY = payload.y;
            event.itemOwnedByYou = payload.isOwnedByYou;
            event.ownerLockRemainingMs = payload.ownerLockRemainingMs;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::WorldItemDespawn: {
            world::WorldItemDespawnPayload payload;
            std::string decodeError;
            if (!world::DecodeWorldItemDespawn(packet.payload.data(), packet.payload.size(),
                                              payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::WorldItemDespawnEvent;
            event.dropEntityId = payload.dropEntityId;
            event.itemDespawnReason = payload.reason;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::ItemPickupResponse: {
            world::ItemPickupResponsePayload payload;
            std::string decodeError;
            if (!world::DecodeItemPickupResponse(packet.payload.data(), packet.payload.size(),
                                                payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::ItemPickupResponseEvent;
            event.requestId = payload.requestId;
            event.dropEntityId = payload.dropEntityId;
            event.success = payload.success;
            event.itemResultCode = payload.resultCode;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::InventorySnapshot: {
            world::InventorySnapshotPayload payload;
            std::string decodeError;
            if (!world::DecodeInventorySnapshot(packet.payload.data(), packet.payload.size(),
                                               payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::InventorySnapshotEvent;
            event.characterId = payload.characterId;
            event.inventoryEntries = payload.entries;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::InventoryDelta: {
            world::InventoryDeltaPayload payload;
            std::string decodeError;
            if (!world::DecodeInventoryDelta(packet.payload.data(), packet.payload.size(),
                                            payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::InventoryDeltaEvent;
            event.characterId = payload.characterId;
            event.inventoryInstanceId = payload.entry.instanceId;
            event.itemDefinitionId = payload.entry.definitionId;
            event.itemQuantity = payload.entry.quantity;
            event.inventorySlotIndex = payload.entry.slotIndex;
            event.inventoryOpcode = payload.opcode; // 1=Set 2=Remove
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::EquipItemResponse: {
            world::EquipItemResponsePayload payload;
            std::string decodeError;
            if (!world::DecodeEquipItemResponse(packet.payload.data(), packet.payload.size(),
                                               payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::EquipItemResponseEvent;
            event.requestId = payload.requestId;
            event.success = payload.success;
            event.itemResultCode = payload.resultCode;
            event.itemEquipmentSlot = payload.equipmentSlot;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::UnequipItemResponse: {
            world::UnequipItemResponsePayload payload;
            std::string decodeError;
            if (!world::DecodeUnequipItemResponse(packet.payload.data(), packet.payload.size(),
                                                 payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::UnequipItemResponseEvent;
            event.requestId = payload.requestId;
            event.success = payload.success;
            event.itemResultCode = payload.resultCode;
            event.itemEquipmentSlot = payload.equipmentSlot;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::EquipmentSnapshot: {
            world::EquipmentSnapshotPayload payload;
            std::string decodeError;
            if (!world::DecodeEquipmentSnapshot(packet.payload.data(), packet.payload.size(),
                                               payload, decodeError)) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::EquipmentSnapshotEvent;
            event.characterId = payload.characterId;
            event.equipmentSnapshot = payload;
            event.serverTime = payload.serverTime;
            PushEvent(std::move(event));
            return;
        }
        case MessageId::HeartbeatPong: {
            legend::network::ByteReader reader(packet.payload.data(), packet.payload.size());
            reader.ReadUInt32(); // pingSequence
            reader.ReadUInt64(); // serverTimeMs
            if (!reader.IsValid() || reader.Remaining() != 0) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            const float rtt = static_cast<float>(
                std::chrono::duration<double, std::milli>(now - m_lastPingTime).count());
            m_lastRttMs.store(rtt);
            m_lastPongTime = now;
            return;
        }
        case MessageId::WorldErrorResponse: {
            legend::network::ByteReader reader(packet.payload.data(), packet.payload.size());
            const std::uint16_t errorCode = reader.ReadUInt16();
            std::string message;
            (void)(reader.ReadString(message));
            if (!reader.IsValid() || reader.Remaining() != 0) {
                return;
            }
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::ProtocolError;
            event.errorCode = errorCode;
            event.message = message;
            PushEvent(std::move(event));
            return;
        }
        default: {
            WorldNetworkEvent event;
            event.type = WorldNetworkEvent::Type::ProtocolError;
            event.message = "unknown message id " + std::to_string(packet.header.messageId);
            PushEvent(std::move(event));
            Disconnect(false);
            return;
        }
    }
}

void WorldNetworkClient::ScheduleHeartbeat() {
    if (m_heartbeatScheduled.exchange(true)) {
        return;
    }
    m_heartbeatTimer.expires_after(
        std::chrono::milliseconds(static_cast<int>(m_config.heartbeatIntervalSeconds * 1000)));
    auto self = shared_from_this();
    m_heartbeatTimer.async_wait([self](const std::error_code& ec) {
        self->m_heartbeatScheduled.store(false);
        if (ec || self->m_state.load() != WorldFlowState::WorldReady || !self->m_connection) {
            return;
        }
        self->SendHeartbeat();
        self->ScheduleHeartbeat();
    });
}

void WorldNetworkClient::SendHeartbeat() {
    if (!m_connection) {
        return;
    }
    HeartbeatPingPayload ping;
    ping.pingSequence = ++m_pingSequence;
    ping.clientTimeMs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    m_lastPingTime = std::chrono::steady_clock::now();
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::HeartbeatPing);
    if (EncodeHeartbeatPing(ping, out.payload)) {
        m_connection->Send(out);
    }
}

void WorldNetworkClient::PushEvent(WorldNetworkEvent event) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_events.push_back(std::move(event));
}

void WorldNetworkClient::SetState(WorldFlowState state, const std::string& error) {
    m_state.store(state);
    if (!error.empty()) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = error;
    }
}

std::string WorldNetworkClient::LastError() const {
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_lastError;
}

} // namespace legend::client
