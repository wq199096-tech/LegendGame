#include "Server/Gateway/GatewaySession.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::gateway {

using legend::network::ByteReader;
using legend::network::ByteWriter;
using legend::network::ClientHelloPayload;
using legend::network::EncodeHeartbeatPong;
using legend::network::EncodeLoginResponse;
using legend::network::EncodeServerHello;
using legend::network::HeartbeatPingPayload;
using legend::network::HeartbeatPongPayload;
using legend::network::LoginErrorCode;
using legend::network::LoginRequestPayload;
using legend::network::LoginResponsePayload;
using legend::network::MessageId;
using legend::network::Packet;
using legend::network::ServerHelloPayload;

GatewaySession::GatewaySession(legend::net::TcpConnectionPtr connection,
                               std::uint64_t connectionId)
    : m_connection(std::move(connection)), m_connectionId(connectionId) {}

void GatewaySession::SendPacket(const Packet& packet) {
    m_connection->Send(packet);
}

void GatewaySession::Disconnect() {
    m_state = GatewaySessionState::Closing;
    // 阶段9.1：flush 写队列后再关（拒绝握手的 ServerHello 要先落盘）
    m_connection->CloseAfterFlush();
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
    // 阶段9.1指令十八：统一 Shared Protocol（不再手写 ByteReader）
    ClientHelloPayload hello;
    if (!legend::network::DecodeClientHello(packet.payload.data(), packet.payload.size(),
                                            hello, error)) {
        error = "malformed ClientHello";
        return false;
    }
    // 指令四十六：版本不一致 -> accepted=false + 断开
    const bool accepted = hello.protocolVersion == legend::network::kProtocolVersion;

    ServerHelloPayload response;
    response.accepted = accepted;
    response.protocolVersion = legend::network::kProtocolVersion;
    response.connectionId = m_connectionId;
    response.serverName = "LegendGateway";
    response.message = accepted ? "welcome" : "protocol version mismatch";
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::ServerHello);
    if (!EncodeServerHello(response, out.payload)) {
        error = "encode ServerHello failed";
        return false;
    }
    SendPacket(out);
    LOG_INFO("[Gateway] ServerHello(accepted=" + std::string(accepted ? "true" : "false") +
             ") queued for #" + std::to_string(m_connectionId));

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
            // 阶段9.1指令十九/二十：统一 Protocol——Pong 只回 pingSequence+serverTimeMs
            HeartbeatPingPayload ping;
            if (!legend::network::DecodeHeartbeatPing(packet.payload.data(),
                                                      packet.payload.size(), ping, error)) {
                error = "malformed HeartbeatPing";
                return false;
            }
            m_lastPingSequence = ping.pingSequence;
            HeartbeatPongPayload pong;
            pong.pingSequence = ping.pingSequence;
            pong.serverTimeMs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
            Packet out;
            out.header.messageId = static_cast<std::uint16_t>(MessageId::HeartbeatPong);
            if (!EncodeHeartbeatPong(pong, out.payload)) {
                error = "encode HeartbeatPong failed";
                return false;
            }
            SendPacket(out);
            return true;
        }
        case MessageId::LoginRequest: {
            // 指令七十九/八十：重复登录拒绝（不重复转发）
            if (m_state == GatewaySessionState::LoginPending) {
                SendLoginError(LoginErrorCode::AlreadyPending, "login already pending");
                return true; // 不算协议错误，不断开
            }
            if (m_state == GatewaySessionState::Authenticated) {
                SendLoginError(LoginErrorCode::AlreadyAuthenticated, "already authenticated");
                return true;
            }
            LoginRequestPayload request;
            if (!legend::network::DecodeLoginRequest(packet.payload.data(),
                                                     packet.payload.size(), request, error)) {
                error = "malformed LoginRequest";
                return false; // 指令一百一十三：payload 缺字段 -> Protocol Error 断开
            }
            m_username = request.username;
            m_state = GatewaySessionState::LoginPending;
            m_pendingUsername = request.username;
            m_pendingToken = request.token;
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

void GatewaySession::CompleteLogin(bool success, std::uint64_t accountId,
                                   const std::string& displayName, const std::string& message) {
    if (m_state != GatewaySessionState::LoginPending) {
        return;
    }
    // 阶段9.1指令七：只更新状态/保存账号——发送由 GatewayServer 统一单发
    m_accountId = accountId;
    m_displayName = displayName;
    m_state = success ? GatewaySessionState::Authenticated : GatewaySessionState::HandshakeCompleted;
}

void GatewaySession::SendLoginError(LoginErrorCode errorCode, const std::string& message) {
    // 阶段9.1指令八：单一发送点（EncodeLoginResponse + 错误码）
    LoginResponsePayload response;
    response.success = false;
    response.accountId = 0;
    response.errorCode = static_cast<std::uint16_t>(errorCode);
    response.displayName = "";
    response.message = message;
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::LoginResponse);
    if (EncodeLoginResponse(response, packet.payload)) {
        SendPacket(packet);
    }
}

} // namespace legend::gateway
