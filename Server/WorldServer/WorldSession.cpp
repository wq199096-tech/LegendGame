#include "Server/WorldServer/WorldSession.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/Protocol.h"
#include "Shared/World/WorldProtocol.h"
#include "Shared/World/WorldTypes.h"

namespace legend::world {

using legend::network::ClientHelloPayload;
using legend::network::DecodeClientHello;
using legend::network::DecodeHeartbeatPing;
using legend::network::EncodeHeartbeatPong;
using legend::network::EncodeServerHello;
using legend::network::HeartbeatPingPayload;
using legend::network::HeartbeatPongPayload;
using legend::network::MessageId;
using legend::network::Packet;
using legend::network::ServerHelloPayload;

WorldSession::WorldSession(legend::net::TcpConnectionPtr connection, std::uint64_t connectionId)
    : m_connection(std::move(connection)), m_connectionId(connectionId) {}

void WorldSession::SendPacket(const Packet& packet) {
    m_connection->Send(packet);
}

void WorldSession::Disconnect() {
    m_state = WorldSessionState::Closing;
    m_connection->CloseAfterFlush();
}

bool WorldSession::OnPacket(const Packet& packet, std::string& error) {
    m_lastPacketTime = std::chrono::steady_clock::now();
    if (m_state == WorldSessionState::Connected) {
        // 阶段11 指令十七：握手前只允许 WorldClientHello(200)
        if (static_cast<MessageId>(packet.header.messageId) != MessageId::WorldClientHello) {
            error = "handshake required before other messages";
            return false;
        }
        return HandleHandshake(packet, error);
    }
    return HandlePostHandshake(packet, error);
}

bool WorldSession::HandleHandshake(const Packet& packet, std::string& error) {
    // WorldClientHello 与阶段9 ClientHello 字节布局一致（指令十七），复用解码。
    ClientHelloPayload hello;
    if (!legend::network::DecodeClientHello(packet.payload.data(), packet.payload.size(), hello,
                                            error)) {
        error = "malformed WorldClientHello";
        return false;
    }
    const bool accepted = hello.protocolVersion == world::kWorldProtocolVersion;
    LOG_INFO("[World] WorldClientHello version=" + std::to_string(hello.protocolVersion) +
             " expected=" + std::to_string(world::kWorldProtocolVersion));
    // WorldServerHello 与阶段9 ServerHello 字节布局一致（指令十八）。
    ServerHelloPayload response;
    response.accepted = accepted;
    response.protocolVersion = world::kWorldProtocolVersion;
    response.connectionId = m_connectionId;
    response.serverName = "LegendWorldServer";
    response.message = accepted ? "welcome" : "protocol version mismatch";
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::WorldServerHello);
    if (!EncodeServerHello(response, out.payload)) {
        error = "encode WorldServerHello failed";
        return false;
    }
    SendPacket(out);
    LOG_INFO("[World] ServerHello(accepted=" + std::string(accepted ? "true" : "false") +
             ") queued for #" + std::to_string(m_connectionId));
    if (!accepted) {
        // 指令八十六：版本拒绝 -> accepted=false -> CloseAfterFlush
        error = "protocol version mismatch";
        return false;
    }
    m_state = WorldSessionState::WaitingEnterWorld;
    return true;
}

bool WorldSession::HandlePostHandshake(const Packet& packet, std::string& error) {
    switch (static_cast<MessageId>(packet.header.messageId)) {
        case MessageId::HeartbeatPing: {
            // 阶段11 指令七十七：复用 HeartbeatPing/Pong（5s Ping / 15s timeout）。
            HeartbeatPingPayload ping;
            if (!legend::network::DecodeHeartbeatPing(packet.payload.data(),
                                                      packet.payload.size(), ping, error)) {
                error = "malformed HeartbeatPing";
                return false;
            }
            HeartbeatPongPayload pong;
            pong.pingSequence = ping.pingSequence;
            pong.serverTimeMs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
            Packet out;
            out.header.messageId = static_cast<std::uint16_t>(MessageId::HeartbeatPong);
            if (!legend::network::EncodeHeartbeatPong(pong, out.payload)) {
                error = "encode HeartbeatPong failed";
                return false;
            }
            SendPacket(out);
            return true;
        }
        case MessageId::EnterWorldRequest: {
            // 阶段11 指令十九：EnterWorldRequest（WaitingEnterWorld 才接受）
            if (m_state != WorldSessionState::WaitingEnterWorld) {
                error = "EnterWorldRequest in invalid state";
                return false;
            }
            if (m_hasPendingEnterWorld) {
                error = "enter world already pending";
                return false;
            }
            m_pendingEnterWorld = packet;
            m_hasPendingEnterWorld = true;
            return true;
        }
        case MessageId::PlayerMoveInput:
        case MessageId::PlayerAttackRequest:
        case MessageId::SkillCastRequest:
        case MessageId::ItemPickupRequest:
        case MessageId::EquipItemRequest:
        case MessageId::UnequipItemRequest:
        case MessageId::QuestAcceptRequest:
        case MessageId::QuestTurnInRequest:
        case MessageId::QuestAbandonRequest:
        case MessageId::NpcInteractRequest:
        case MessageId::DialogueOptionRequest:
        case MessageId::ShopOpenRequest:
        case MessageId::ShopBuyRequest:
        case MessageId::ShopSellRequest:
        case MessageId::TeleportRequest:
        case MessageId::PortalUseRequest:
        case MessageId::RespawnRequest:
            // 阶段11 指令三十四：移动输入放行（InWorld 校验在 WorldServer/
            // WorldMapManager：非 InWorld 玩家直接忽略）。
            // 阶段14 指令三十八：攻击请求放行（验证在 CombatService/WorldServer）。
            // 阶段15 指令二十二：技能施放请求放行（验证在 SkillService/WorldServer）。
            // 阶段18 指令二十一/二十七/三十四：拾取/装备/卸下请求放行
            //（校验在 WorldServer 服务器权威逻辑）。
            // 阶段19 指令二：Quest Accept/TurnIn/Abandon 请求放行（校验在
            // WorldServer/QuestService；Client 不能上报进度/状态/奖励）。
            // 阶段21 指令十九/三十三：PortalUse/Respawn 请求放行（全部验证链在
            // WorldServer 服务器权威逻辑）。
            return true;
        case MessageId::LeaveWorldRequest: {
            // 阶段26 指令十七：主动离开世界（仅 InWorld 接受；其它状态忽略=Protocol error）。
            if (m_state != WorldSessionState::InWorld || m_hasPendingLeaveWorld) {
                error = "LeaveWorldRequest in invalid state";
                return false;
            }
            world::LeaveWorldRequestPayload req;
            if (!legend::world::DecodeLeaveWorldRequest(packet.payload.data(), packet.payload.size(),
                                                        req, error)) {
                error = "malformed LeaveWorldRequest";
                return false;
            }
            m_pendingLeaveWorld = packet;
            m_hasPendingLeaveWorld = true;
            return true;
        }
        case MessageId::WorldDisconnectNotice: {
            // 阶段11 指令七十九：客户端主动退出世界
            world::WorldDisconnectNoticePayload notice;
            if (!legend::world::DecodeWorldDisconnectNotice(packet.payload.data(),
                                                            packet.payload.size(), notice,
                                                            error)) {
                error = "malformed WorldDisconnectNotice";
                return false;
            }
            m_state = WorldSessionState::Closing;
            m_connection->Close();
            return true;
        }
        default:
            // 阶段11 指令六十三/一百零八：未知消息 -> Protocol Error 只断当前 Client
            error = "unknown message id";
            return false;
    }
}

bool WorldSession::TakePendingEnterWorld(Packet& out) {
    if (!m_hasPendingEnterWorld) {
        return false;
    }
    out = std::move(m_pendingEnterWorld);
    m_pendingEnterWorld = Packet{};
    m_hasPendingEnterWorld = false;
    return true;
}

bool WorldSession::TakePendingLeaveWorld(Packet& out) {
    if (!m_hasPendingLeaveWorld) {
        return false;
    }
    out = std::move(m_pendingLeaveWorld);
    m_pendingLeaveWorld = Packet{};
    m_hasPendingLeaveWorld = false;
    return true;
}

} // namespace legend::world
