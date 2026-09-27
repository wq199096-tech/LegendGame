#include "Server/Gateway/GatewayServer.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"
#include "Shared/Network/Protocol.h" // 阶段9.3：LoginErrorCode/payload Encode/Decode

#include <chrono>

namespace legend::gateway {

using legend::network::ByteReader;
using legend::network::ByteWriter;
using legend::network::ClientHelloPayload;
using legend::network::DecodeLoginGatewayResponse;
using legend::network::DecodeServerHello;
using legend::network::EncodeClientHello;
using legend::network::EncodeGatewayLoginForward;
using legend::network::EncodeLoginResponse;
using legend::network::GatewayLoginForwardPayload;
using legend::network::kProtocolVersion;
using legend::network::LoginErrorCode;
using legend::network::LoginGatewayResponsePayload;
using legend::network::LoginResponsePayload;
using legend::network::MessageId;
using legend::network::Packet;
using legend::network::ServerHelloPayload;
namespace net = legend::net;

GatewayServer::GatewayServer(net::NetworkService& service, const GatewayConfig& config)
    : m_service(service),
      m_config(config),
      m_server(std::make_shared<net::TcpServer>(service)),
      m_loginClient(std::make_shared<net::TcpClient>(service)),
      m_reconnectTimer(service.Io()),
      m_pendingTimer(service.Io()),
      m_idleTimer(service.Io()) {}

bool GatewayServer::Start(std::string& error) {
    if (!m_server->Listen(m_config.listenPort, error)) {
        return false;
    }
    m_server->StartAccepting(
        [this](net::TcpConnectionPtr connection) { OnClientAccepted(std::move(connection)); });
    ConnectToLogin(); // 指令四十八：Gateway 启动后建立到 LoginServer 的连接
    SchedulePendingTimeoutCheck();
    ScheduleIdleTimeoutCheck(); // idle scan
    return true;
}

void GatewayServer::Stop() {
    m_stopped.store(true);
    m_reconnectTimer.cancel();
    m_pendingTimer.cancel();
    m_idleTimer.cancel();
    m_loginClient->Cancel();
    if (m_loginConnection) {
        m_loginConnection->Close();
        m_loginConnection.reset();
    }
    m_loginAvailable.store(false);
    m_server->Stop();
    m_sessions.clear();
    m_pendingLogins.clear();
}

std::size_t GatewayServer::ClientCount() const {
    return m_sessions.size();
}

void GatewayServer::OnClientAccepted(net::TcpConnectionPtr connection) {
    const std::uint64_t id = connection->Id();
    LOG_INFO("[Gateway] Client #" + std::to_string(id) + " connected.");
    auto session = std::make_shared<GatewaySession>(connection, id);
    m_sessions[id] = session;
    connection->Start(
        [this, id](const Packet& packet) { OnClientPacket(id, packet); },
        [this](std::uint64_t closedId, const std::error_code& ec) {
            OnClientClosed(closedId, ec);
        });
}

void GatewayServer::OnClientPacket(std::uint64_t connectionId, const Packet& packet) {
    // 阶段9.1指令三十九：每包日志删除（心跳会刷屏）；INFO 只保留
    // connect/handshake/login/disconnect/timeout
    auto it = m_sessions.find(connectionId);
    if (it == m_sessions.end()) {
        return;
    }
    auto session = it->second;
    std::string error;
    if (!session->OnPacket(packet, error)) {
        LOG_INFO("[Gateway] Client #" + std::to_string(connectionId) + " rejected: " + error);
        // 指令六十九/七十：协议错误 -> WARN + 断开（Gateway 进程保持运行，指令一百一十三）
        if (m_hooks.onClientClosed) {
            m_hooks.onClientClosed(connectionId, error);
        }
        m_sessions.erase(it);
        session->Disconnect();
        return;
    }
    if (session->State() == GatewaySessionState::HandshakeCompleted && m_hooks.onClientHandshakeComplete) {
        m_hooks.onClientHandshakeComplete(connectionId);
    }
    // 指令五十五：LoginPending -> 取出暂存数据 -> GatewayLoginForward
    std::string username;
    std::string token;
    if (session->TakePendingLogin(username, token)) {
        ForwardLogin(connectionId, username, token);
    }
}

void GatewayServer::OnClientClosed(std::uint64_t connectionId, const std::error_code& ec) {
    m_sessions.erase(connectionId);
    // 指令六十一：Client 断开 -> 清理该连接的 Pending Login（防无限增长）
    for (auto it = m_pendingLogins.begin(); it != m_pendingLogins.end();) {
        if (it->second.clientConnectionId == connectionId) {
            it = m_pendingLogins.erase(it);
        } else {
            ++it;
        }
    }
    if (m_hooks.onClientClosed) {
        m_hooks.onClientClosed(connectionId,
                               ec ? std::string(ec.message()) : std::string("closed"));
    }
}

void GatewayServer::ConnectToLogin() {
    if (m_stopped.load()) {
        return;
    }
    auto self = shared_from_this();
    m_loginClient->Connect(
        m_config.loginHost, m_config.loginPort,
        [this, self](net::TcpConnectionPtr connection) { OnLoginConnected(std::move(connection)); },
        [this, self](const std::error_code&) {
            // 指令四十九：Login 未启动 -> Gateway 仍在线，按节奏重连
            m_loginAvailable.store(false);
            if (m_hooks.onLoginConnectionChanged) {
                m_hooks.onLoginConnectionChanged(false);
            }
            ScheduleLoginReconnect();
        });
}

void GatewayServer::ScheduleLoginReconnect() {
    if (m_stopped.load() || m_reconnectScheduled.exchange(true)) {
        return;
    }
    m_reconnectTimer.expires_after(
        std::chrono::milliseconds(static_cast<int>(m_config.loginReconnectSeconds * 1000)));
    auto self = shared_from_this();
    m_reconnectTimer.async_wait([this, self](const std::error_code& ec) {
        m_reconnectScheduled.store(false);
        if (ec || m_stopped.load() || m_loginConnection || m_loginAvailable.load()) {
            return;
        }
        ConnectToLogin(); // 指令五十：每 2 秒（默认）重试
    });
}

void GatewayServer::OnLoginConnected(net::TcpConnectionPtr connection) {
    // 指令九：TCP 连上绝不立即 loginAvailable=true——先完成内部握手
    if (m_loginConnection) {
        auto stale = m_loginConnection;
        m_loginConnection.reset();
        stale->Close();
    }
    m_loginConnection = connection;
    m_loginAvailable.store(false);
    m_loginHandshakeDone = false;
    connection->Start(
        [this, linkId = connection->Id()](const Packet& packet) { OnLoginPacket(linkId, packet); },
        [this, self = shared_from_this()](std::uint64_t, const std::error_code&) {
            m_loginConnection.reset();
            m_loginHandshakeDone = false;
            m_loginAvailable.store(false);
            if (m_hooks.onLoginConnectionChanged) {
                m_hooks.onLoginConnectionChanged(false);
            }
            ScheduleLoginReconnect(); // 断线自动重连
        });
    // 指令九：发送内部 ClientHello（Shared Protocol，标识 LegendGateway）
    ClientHelloPayload hello;
    hello.protocolVersion = kProtocolVersion;
    hello.clientBuild = "0.9.0";
    hello.clientName = "LegendGateway";
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::ClientHello);
    if (!EncodeClientHello(hello, out.payload)) {
        connection->Close();
        return;
    }
    connection->Send(out);
}

void GatewayServer::OnLoginClosed(const std::error_code&) {}

void GatewayServer::OnLoginPacket(std::uint64_t linkId, const Packet& packet) {
    // 闄堟棫閾捐矾瀹堝崼锛氬彧鏈夊綋鍓?m_loginConnection 鐨勫寘鎵嶈澶勭悊
    if (!m_loginConnection || m_loginConnection->Id() != linkId) {
        return;
    }
    const auto messageId = static_cast<MessageId>(packet.header.messageId);
    if (!m_loginHandshakeDone) {
        // 指令九：内部握手完成前只接受 ServerHello，其余一律关闭
        if (messageId != MessageId::ServerHello) {
            if (m_loginConnection) {
                m_loginConnection->Close();
            }
            return;
        }
        ServerHelloPayload hello;
        std::string error;
        if (!DecodeServerHello(packet.payload.data(), packet.payload.size(), hello, error) ||
            !hello.accepted) {
            // 指令十：ServerHello accepted=false -> 关闭连接 -> ScheduleLoginReconnect
            if (m_loginConnection) {
                m_loginConnection->Close();
            }
            return;
        }
        // 指令九：accepted=true -> 此时 loginAvailable=true，LoginRequest 才允许 Forward
        m_loginHandshakeDone = true;
        m_loginAvailable.store(true);
        LOG_INFO("[Gateway] Login handshake accepted (server=" + hello.serverName + ").");
        if (m_hooks.onLoginConnectionChanged) {
            m_hooks.onLoginConnectionChanged(true);
        }
        return;
    }
    switch (messageId) {
        case MessageId::LoginGatewayResponse:
            HandleLoginGatewayResponse(packet);
            break;
        default:
            break; // LoginServer 其它消息阶段9忽略
    }
}

void GatewayServer::ForwardLogin(std::uint64_t clientConnectionId, const std::string& username,
                                 const std::string& token) {
    if (!m_loginAvailable.load() || !m_loginConnection) {
        // 指令四十九 + 阶段9.3指令七：ServiceUnavailable 单发（EncodeLoginResponse + 错误码）
        SendLoginError(clientConnectionId, LoginErrorCode::ServiceUnavailable,
                       "login service unavailable");
        if (auto it = m_sessions.find(clientConnectionId); it != m_sessions.end()) {
            it->second->CompleteLogin(false, 0, "", "login service unavailable");
        }
        return;
    }
    const std::uint64_t requestId = m_nextRequestId++;
    PendingLogin pending;
    pending.requestId = requestId;
    pending.clientConnectionId = clientConnectionId;
    m_pendingLogins[requestId] = pending; // 指令五十九：requestId -> clientConnectionId

    Packet forward;
    forward.header.messageId = static_cast<std::uint16_t>(MessageId::GatewayLoginForward);
    // 阶段9.3指令三：EncodeGatewayLoginForward（Shared Protocol）
    GatewayLoginForwardPayload payload;
    payload.requestId = requestId;
    payload.clientConnectionId = clientConnectionId;
    payload.username = username;
    payload.token = token; // 仅内部转发；日志不打印 token（指令五十二/八十九）
    if (!EncodeGatewayLoginForward(payload, forward.payload)) {
        m_pendingLogins.erase(requestId);
        return;
    }
    m_loginConnection->Send(forward);
}

void GatewayServer::HandleLoginGatewayResponse(const Packet& packet) {
    // 阶段9.2/9.3指令四：DecodeLoginGatewayResponse（Shared Protocol，accountId uint64）
    LoginGatewayResponsePayload response;
    std::string decodeError;
    if (!DecodeLoginGatewayResponse(packet.payload.data(), packet.payload.size(), response,
                                    decodeError)) {
        return;
    }
    auto it = m_pendingLogins.find(response.requestId);
    if (it == m_pendingLogins.end()) {
        return; // 超时已清理/Client 已断开 -> 丢弃（指令六十/六十一）
    }
    const std::uint64_t clientConnectionId = response.clientConnectionId;
    m_pendingLogins.erase(it);
    // 阶段9.3指令五：EncodeLoginResponse 单发（success/accountId u64/errorCode u16）
    LoginResponsePayload out;
    out.success = response.success;
    out.accountId = response.accountId;
    out.errorCode = response.errorCode;
    out.displayName = response.displayName;
    out.message = response.message;
    SendToClient(clientConnectionId, [&] {
        Packet packet;
        packet.header.messageId = static_cast<std::uint16_t>(MessageId::LoginResponse);
        EncodeLoginResponse(out, packet.payload);
        return packet;
    }());
    if (auto sit = m_sessions.find(clientConnectionId); sit != m_sessions.end()) {
        sit->second->CompleteLogin(response.success, response.accountId, response.displayName,
                                   response.message);
    }
    if (m_hooks.onLoginResult) {
        m_hooks.onLoginResult(clientConnectionId, response.success, response.accountId,
                              response.displayName);
    }
}

void GatewayServer::SendToClient(std::uint64_t connectionId, const Packet& packet) {
    if (auto it = m_sessions.find(connectionId); it != m_sessions.end()) {
        it->second->SendPacket(packet);
    }
}

void GatewayServer::SendLoginError(std::uint64_t connectionId,
                                   legend::network::LoginErrorCode errorCode,
                                   const std::string& message) {
    // 阶段9.3指令六：EncodeLoginResponse + LoginErrorCode（accountId uint64）
    legend::network::LoginResponsePayload out;
    out.success = false;
    out.accountId = 0;
    out.errorCode = static_cast<std::uint16_t>(errorCode);
    out.displayName = "";
    out.message = message;
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(legend::network::MessageId::LoginResponse);
    if (legend::network::EncodeLoginResponse(out, packet.payload)) {
        SendToClient(connectionId, packet);
    }
}

void GatewayServer::CheckPendingTimeouts() {
    if (m_stopped.load()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(m_config.pendingLoginTimeoutSeconds));
    for (auto it = m_pendingLogins.begin(); it != m_pendingLogins.end();) {
        if (now - it->second.createdAt > timeout) {
            // 指令十四：Pending 超时 -> LoginResponse(success=false, Timeout,
            // "login timeout") -> 删 Pending；迟到的 LoginGatewayResponse 因
            // requestId 不存在被丢弃（exactly-once）
            const std::uint64_t clientConnectionId = it->second.clientConnectionId;
            it = m_pendingLogins.erase(it);
            SendLoginError(clientConnectionId, LoginErrorCode::Timeout, "login timeout");
            if (auto sit = m_sessions.find(clientConnectionId); sit != m_sessions.end()) {
                sit->second->CompleteLogin(false, 0, "", "login timeout");
            }
        } else {
            ++it;
        }
    }
    SchedulePendingTimeoutCheck();
    ScheduleIdleTimeoutCheck(); // idle scan
}

void GatewayServer::SchedulePendingTimeoutCheck() {
    if (m_stopped.load()) {
        return;
    }
    m_pendingTimer.expires_after(std::chrono::milliseconds(250));
    auto self = shared_from_this();
    m_pendingTimer.async_wait([this, self](const std::error_code& ec) {
        if (ec) {
            return;
        }
        CheckPendingTimeouts();
    });
}


void GatewayServer::CheckIdleTimeouts() {
    if (m_stopped.load()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto idle = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(m_config.clientIdleTimeoutSeconds));
    for (auto it = m_sessions.begin(); it != m_sessions.end();) {
        auto session = it->second;
        // LoginPending is managed separately by the pending timeout, avoiding a race
        if (session->State() == GatewaySessionState::LoginPending) {
            ++it;
            continue;
        }
        if (now - session->LastPacketTime() > idle) {
            const std::uint64_t id = it->first;
            it = m_sessions.erase(it);
            session->Disconnect();
            LOG_INFO("[Gateway] Client #" + std::to_string(id) + " idle timeout, disconnected.");
            if (m_hooks.onClientClosed) {
                m_hooks.onClientClosed(id, "idle timeout");
            }
        } else {
            ++it;
        }
    }
    ScheduleIdleTimeoutCheck();
}

void GatewayServer::ScheduleIdleTimeoutCheck() {
    if (m_stopped.load()) {
        return;
    }
    m_idleTimer.expires_after(std::chrono::milliseconds(250));
    auto self = shared_from_this();
    m_idleTimer.async_wait([this, self](const std::error_code& ec) {
        if (ec) {
            return;
        }
        CheckIdleTimeouts();
    });
}
} // namespace legend::gateway
