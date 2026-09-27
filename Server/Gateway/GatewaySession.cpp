#include "Server/Gateway/GatewaySession.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::gateway {

using legend::network::ByteReader;
using legend::network::ByteWriter;
using legend::network::MessageId;
using legend::network::Packet;

GatewaySession::GatewaySession(legend::net::TcpConnectionPtr connection,
                               std::uint64_t connectionId)
    : m_connection(std::move(connection)), m_connectionId(connectionId) {}

void GatewaySession::SendPacket(const Packet& packet) {
    m_connection->Send(packet);
}

void GatewaySession::Disconnect() {
    m_state = GatewaySessionState::Closing;
    m_connection->Close();
}

bool GatewaySession::OnPacket(const Packet& packet, std::string& error) {
    m_lastPacketTime = std::chrono::steady_clock::now();
    if (m_state == GatewaySessionState::Connected) {
        // 指令七十六：握手前只允许 ClientHello
        if (static_cast<MessageId>(packet.header.messageId) != MessageId::ClientHello) {
            error = "handshake required before other messages";
            return false;
        }
        return HandleHandshake(packet, error);
    }
    // 指令七十七：握手后允许 LoginRequest / Heartbeat / DisconnectNotice
    return HandlePostHandshake(packet, error);
}

bool GatewaySession::HandleHandshake(const Packet& packet, std::string& error) {
    const auto* data = packet.payload.data();
    ByteReader reader(data, packet.payload.size());
    const std::uint16_t protocolVersion = reader.ReadUInt16();
    std::string clientBuild;
    std::string clientName;
    if (!reader.IsValid() || !reader.ReadString(clientBuild) || !reader.ReadString(clientName)) {
        error = "malformed ClientHello";
        return false;
    }
    // 指令四十六：版本不一致 -> accepted=false + 断开
    const bool accepted = protocolVersion == legend::network::kProtocolVersion;

    Packet response;
    response.header.messageId = static_cast<std::uint16_t>(MessageId::ServerHello);
    ByteWriter writer(response.payload);
    writer.WriteBool(accepted);
    writer.WriteUInt16(legend::network::kProtocolVersion);
    writer.WriteUInt64(m_connectionId);
    if (!writer.WriteString("LegendGateway") || !writer.WriteString(
            accepted ? "welcome" : "protocol version mismatch")) {
        error = "encode ServerHello failed";
        return false;
    }
    SendPacket(response);

    if (!accepted) {
        error = "protocol version mismatch";
        return false; // 断开
    }
    m_state = GatewaySessionState::HandshakeCompleted;
    return true;
}

bool GatewaySession::HandlePostHandshake(const Packet& packet, std::string& error) {
    switch (static_cast<MessageId>(packet.header.messageId)) {
        case MessageId::HeartbeatPing: {
            // 指令六十二/六十三：原样返回 pingSequence + serverTime
            const auto* data = packet.payload.data();
            ByteReader reader(data, packet.payload.size());
            const std::uint32_t pingSequence = reader.ReadUInt32();
            const std::uint64_t clientTimeMs = reader.ReadUInt64();
            if (!reader.IsValid()) {
                error = "malformed HeartbeatPing";
                return false;
            }
            m_lastPingSequence = pingSequence;
            Packet pong;
            pong.header.messageId = static_cast<std::uint16_t>(MessageId::HeartbeatPong);
            ByteWriter writer(pong.payload);
            writer.WriteUInt32(pingSequence);
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            writer.WriteUInt64(
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               now)
                                               .count()));
            writer.WriteUInt64(clientTimeMs);
            SendPacket(pong);
            return true;
        }
        case MessageId::LoginRequest: {
            // 指令七十九/八十：重复登录拒绝（不重复转发）
            if (m_state == GatewaySessionState::LoginPending) {
                SendLoginError("login already pending");
                return true; // 不算协议错误，不断开
            }
            if (m_state == GatewaySessionState::Authenticated) {
                SendLoginError("already authenticated");
                return true;
            }
            const auto* data = packet.payload.data();
            ByteReader reader(data, packet.payload.size());
            std::string username;
            std::string token;
            if (!reader.ReadString(username) || !reader.ReadString(token) ||
                !reader.IsValid()) {
                error = "malformed LoginRequest";
                return false; // 指令一百一十三：payload 缺字段 -> Protocol Error 断开
            }
            m_username = username;
            m_state = GatewaySessionState::LoginPending;
            m_pendingUsername = username;
            m_pendingToken = token;
            m_hasPendingLogin = true; // 仅内存暂存，GatewayServer 取走转发（不落日志）
            return true; // GatewayServer 检测到 LoginPending 后执行转发
        }
        case MessageId::DisconnectNotice: {
            // 指令六十七：Client 主动退出
            m_state = GatewaySessionState::Closing;
            m_connection->Close();
            return true;
        }
        default:
            // 指令七十：未知 messageId -> Protocol Error 断开
            error = "unknown message id";
            return false;
    }
}

bool GatewaySession::TakePendingLogin(std::string& username, std::string& token) {
    if (!m_hasPendingLogin) {
        return false;
    }
    username = m_pendingUsername;
    token = m_pendingToken;
    m_pendingUsername.clear();
    m_pendingToken.clear();
    m_hasPendingLogin = false;
    return true;
}

void GatewaySession::CompleteLogin(bool success, std::uint32_t accountId,
                                   const std::string& displayName, const std::string& message) {
    if (m_state != GatewaySessionState::LoginPending) {
        return;
    }
    Packet response;
    response.header.messageId = static_cast<std::uint16_t>(MessageId::LoginResponse);
    ByteWriter writer(response.payload);
    writer.WriteBool(success);
    writer.WriteUInt32(accountId);
    if (!writer.WriteString(displayName) || !writer.WriteString(message)) {
        return;
    }
    SendPacket(response);
    m_state = success ? GatewaySessionState::Authenticated : GatewaySessionState::HandshakeCompleted;
}

void GatewaySession::SendLoginError(const std::string& message) {
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::LoginResponse);
    ByteWriter writer(packet.payload);
    writer.WriteBool(false);
    writer.WriteUInt32(0);
    if (writer.WriteString("") && writer.WriteString(message)) {
        SendPacket(packet);
    }
}

} // namespace legend::gateway
