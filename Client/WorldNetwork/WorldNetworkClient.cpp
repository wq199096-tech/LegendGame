#include "Client/WorldNetwork/WorldNetworkClient.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/Protocol.h"

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
