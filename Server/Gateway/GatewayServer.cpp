#include "Server/Gateway/GatewayServer.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

#include <chrono>

namespace legend::gateway {

using legend::network::ByteReader;
using legend::network::ByteWriter;
using legend::network::MessageId;
using legend::network::Packet;
namespace net = legend::net;

GatewayServer::GatewayServer(net::NetworkService& service, const GatewayConfig& config)
    : m_service(service),
      m_config(config),
      m_server(service),
      m_loginClient(std::make_shared<net::TcpClient>(service)),
      m_reconnectTimer(service.Io()),
      m_pendingTimer(service.Io()) {}

bool GatewayServer::Start(std::string& error) {
    if (!m_server.Listen(m_config.listenPort, error)) {
        return false;
    }
    m_server.StartAccepting(
        [this](net::TcpConnectionPtr connection) { OnClientAccepted(std::move(connection)); });
    ConnectToLogin(); // 指令四十八：Gateway 启动后建立到 LoginServer 的连接
    SchedulePendingTimeoutCheck();
    return true;
}

void GatewayServer::Stop() {
    m_stopped.store(true);
    m_reconnectTimer.cancel();
    m_pendingTimer.cancel();
    m_loginClient->Cancel();
    if (m_loginConnection) {
        m_loginConnection->Close();
        m_loginConnection.reset();
    }
    m_loginAvailable.store(false);
    m_server.Stop();
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
    LOG_INFO("[Gateway] Client #" + std::to_string(connectionId) + " packet id=" +
             std::to_string(packet.header.messageId));
    auto it = m_sessions.find(connectionId);
    if (it == m_sessions.end()) {
        return;
    }
    auto session = it->second;
    std::string error;
    if (!session->OnPacket(packet, error)) {
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
        if (ec || m_stopped.load() || m_loginAvailable.load()) {
            return;
        }
        ConnectToLogin(); // 指令五十：每 2 秒（默认）重试
    });
}

void GatewayServer::OnLoginConnected(net::TcpConnectionPtr connection) {
    m_loginConnection = connection;
    m_loginAvailable.store(true);
    if (m_hooks.onLoginConnectionChanged) {
        m_hooks.onLoginConnectionChanged(true);
    }
    connection->Start(
        [this](const Packet& packet) { OnLoginPacket(packet); },
        [this, self = shared_from_this()](std::uint64_t, const std::error_code&) {
            m_loginConnection.reset();
            m_loginAvailable.store(false);
            if (m_hooks.onLoginConnectionChanged) {
                m_hooks.onLoginConnectionChanged(false);
            }
            ScheduleLoginReconnect(); // 断线自动重连
        });
}

void GatewayServer::OnLoginClosed(const std::error_code&) {}

void GatewayServer::OnLoginPacket(const Packet& packet) {
    switch (static_cast<MessageId>(packet.header.messageId)) {
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
        // 指令四十九：Login 不可用 -> 直接回 service unavailable（不 Crash）
        SendLoginError(clientConnectionId, "login service unavailable");
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
    ByteWriter writer(forward.payload);
    writer.WriteUInt64(requestId);
    writer.WriteUInt64(clientConnectionId);
    writer.WriteString(username);
    writer.WriteString(token); // 仅内部转发；日志不打印 token（指令五十二/八十九）
    m_loginConnection->Send(forward);
}

void GatewayServer::HandleLoginGatewayResponse(const Packet& packet) {
    ByteReader reader(packet.payload.data(), packet.payload.size());
    const std::uint64_t requestId = reader.ReadUInt64();
    const std::uint64_t clientConnectionId = reader.ReadUInt64();
    const bool success = reader.ReadBool();
    const std::uint32_t accountId = reader.ReadUInt32();
    std::string displayName;
    std::string message;
    if (!reader.IsValid() || !reader.ReadString(displayName) || !reader.ReadString(message)) {
        return;
    }
    auto it = m_pendingLogins.find(requestId);
    if (it == m_pendingLogins.end()) {
        return; // 超时已清理/Client 已断开 -> 丢弃（指令六十/六十一）
    }
    m_pendingLogins.erase(it);
    SendToClient(clientConnectionId, [&] {
        Packet response;
        response.header.messageId = static_cast<std::uint16_t>(MessageId::LoginResponse);
        ByteWriter w(response.payload);
        w.WriteBool(success);
        w.WriteUInt32(accountId);
        w.WriteString(displayName);
        w.WriteString(message);
        return response;
    }());
    if (auto sit = m_sessions.find(clientConnectionId); sit != m_sessions.end()) {
        sit->second->CompleteLogin(success, accountId, displayName, message);
    }
    if (m_hooks.onLoginResult) {
        m_hooks.onLoginResult(clientConnectionId, success, accountId, displayName);
    }
}

void GatewayServer::SendToClient(std::uint64_t connectionId, const Packet& packet) {
    if (auto it = m_sessions.find(connectionId); it != m_sessions.end()) {
        it->second->SendPacket(packet);
    }
}

void GatewayServer::SendLoginError(std::uint64_t connectionId, const std::string& message) {
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::LoginResponse);
    ByteWriter writer(packet.payload);
    writer.WriteBool(false);
    writer.WriteUInt32(0);
    writer.WriteString("");
    writer.WriteString(message);
    SendToClient(connectionId, packet);
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
            // 指令六十：3~5 秒无响应 -> 删 Pending -> Client 收 login timeout
            const std::uint64_t clientConnectionId = it->second.clientConnectionId;
            it = m_pendingLogins.erase(it);
            if (auto sit = m_sessions.find(clientConnectionId); sit != m_sessions.end()) {
                sit->second->CompleteLogin(false, 0, "", "login timeout");
            }
        } else {
            ++it;
        }
    }
    SchedulePendingTimeoutCheck();
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

} // namespace legend::gateway
