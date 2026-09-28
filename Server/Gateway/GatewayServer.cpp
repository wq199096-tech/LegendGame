#include "Server/Gateway/GatewayServer.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Account/AccountError.h"
#include "Shared/Account/AccountProtocol.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"
#include "Shared/Network/Protocol.h" // 阶段9.3：LoginErrorCode/payload Encode/Decode

#include <chrono>

namespace legend::gateway {

using legend::account::AccountEnvelope;
using legend::account::AccountErrorCode;
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
namespace account = legend::account;

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
    // 阶段10：与 io 线程回调互斥后清表（数据竞态修复）
    {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
        m_sessions.clear();
        m_pendingLogins.clear();
        m_pendingAccounts.clear();
    }
}

std::size_t GatewayServer::ClientCount() const {
    std::lock_guard<std::mutex> lock(m_mapsMutex);
    return m_sessions.size();
}

void GatewayServer::OnClientAccepted(net::TcpConnectionPtr connection) {
    const std::uint64_t id = connection->Id();
    LOG_INFO("[Gateway] Client #" + std::to_string(id) + " connected.");
    auto session = std::make_shared<GatewaySession>(connection, id);
    {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
        m_sessions[id] = session;
    }
    // 阶段10 UAF 修复：回调持 shared_from_this 保活（持有者销毁后挂起回调仍安全）
    auto self = shared_from_this();
    connection->Start(
        [self, id](const Packet& packet) { self->OnClientPacket(id, packet); },
        [self](std::uint64_t closedId, const std::error_code& ec) {
            self->OnClientClosed(closedId, ec);
        });
}

void GatewayServer::OnClientPacket(std::uint64_t connectionId, const Packet& packet) {
    // 阶段9.1指令三十九：每包日志删除（心跳会刷屏）；INFO 只保留
    // connect/handshake/login/disconnect/timeout
    std::shared_ptr<GatewaySession> session;
    {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
        auto it = m_sessions.find(connectionId);
        if (it == m_sessions.end()) {
            return;
        }
        session = it->second;
    }
    std::string error;
    if (!session->OnPacket(packet, error)) {
        LOG_INFO("[Gateway] Client #" + std::to_string(connectionId) + " rejected: " + error);
        // 指令六十九/七十：协议错误 -> WARN + 断开（Gateway 进程保持运行，指令一百一十三）
        if (m_hooks.onClientClosed) {
            m_hooks.onClientClosed(connectionId, error);
        }
        {
            std::lock_guard<std::mutex> lock(m_mapsMutex);
            m_sessions.erase(connectionId);
        }
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
    // 阶段10 指令五十四：Account 请求 -> 包装信封转发（不解析 payload）
    Packet accountPacket;
    if (session->TakePendingAccountForward(accountPacket)) {
        ForwardAccountPacket(connectionId, accountPacket);
    }
}

void GatewayServer::OnClientClosed(std::uint64_t connectionId, const std::error_code& ec) {
    {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
        m_sessions.erase(connectionId);
        // 指令六十一：Client 断开 -> 清理该连接的 Pending Login（防无限增长）
        for (auto it = m_pendingLogins.begin(); it != m_pendingLogins.end();) {
            if (it->second.clientConnectionId == connectionId) {
                it = m_pendingLogins.erase(it);
            } else {
                ++it;
            }
        }
        // 阶段10：Client 断开 -> 清理该连接的 Account pending（响应到达后自然丢弃）
        for (auto it = m_pendingAccounts.begin(); it != m_pendingAccounts.end();) {
            if (it->second.clientConnectionId == connectionId) {
                it = m_pendingAccounts.erase(it);
            } else {
                ++it;
            }
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
    auto self = shared_from_this();
    connection->Start(
        [self, linkId = connection->Id()](const Packet& packet) {
            self->OnLoginPacket(linkId, packet);
        },
        [self](std::uint64_t, const std::error_code&) {
            self->HandleLoginLinkClosed();
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

void GatewayServer::HandleLoginLinkClosed() {
    // 阶段10：Login 链路关闭统一入口（io 线程；self 保活）
    m_loginConnection.reset();
    m_loginHandshakeDone = false;
    m_loginAvailable.store(false);
    if (m_hooks.onLoginConnectionChanged) {
        m_hooks.onLoginConnectionChanged(false);
    }
    ScheduleLoginReconnect(); // 断线自动重连
}

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
        case MessageId::GatewayAccountResponse:
            // 阶段10 指令五十四：解信封 -> 按 requestId 找 pending -> 回发 Client
            HandleAccountResponse(packet);
            break;
        default:
            break; // LoginServer 其它消息阶段9忽略
    }
}

// ---------------------------------------------------------------------------
// 阶段10：Account 信封转发（Gateway 只搬运，不解析业务 payload —— 指令五十四）
// ---------------------------------------------------------------------------

void GatewayServer::ForwardAccountPacket(std::uint64_t clientConnectionId,
                                         const Packet& packet) {
    if (!m_loginAvailable.load() || !m_loginConnection) {
        const std::uint16_t responseId =
            legend::network::AccountResponseMessageId(packet.header.messageId);
        std::shared_ptr<GatewaySession> session;
        {
            std::lock_guard<std::mutex> lock(m_mapsMutex);
            if (auto it = m_sessions.find(clientConnectionId); it != m_sessions.end()) {
                session = it->second;
            }
        }
        SendAccountErrorResponse(clientConnectionId, 0, responseId,
                                 AccountErrorCode::ServiceUnavailable,
                                 "account service unavailable");
        if (session) {
            session->EndAccount(packet.header.messageId);
        }
        return;
    }
    const std::uint64_t requestId = m_nextRequestId++;
    const bool isWrite = legend::network::IsAccountWriteMessageId(packet.header.messageId);
    std::shared_ptr<GatewaySession> session;
    bool duplicate = false;
    {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
        auto sessionIt = m_sessions.find(clientConnectionId);
        if (sessionIt == m_sessions.end()) {
            return;
        }
        session = sessionIt->second;
        // 阶段10 指令一百零二：同 Session 同种写请求未完成时拒绝重复
        //（读请求如 CharacterList 不去重，可并发）
        if (isWrite && !session->TryBeginAccount(packet.header.messageId, requestId)) {
            duplicate = true;
        } else {
            PendingAccount pending;
            pending.clientConnectionId = clientConnectionId;
            pending.innerMessageId = packet.header.messageId;
            m_pendingAccounts[requestId] = pending;
        }
    }
    if (duplicate) {
        // 锁外发送（SendAccountErrorResponse 内部会加锁，禁止重入）
        SendAccountErrorResponse(clientConnectionId, requestId,
                                 legend::network::AccountResponseMessageId(packet.header.messageId),
                                 AccountErrorCode::RequestPending, "request already pending");
        return;
    }

    AccountEnvelope envelope;
    envelope.requestId = requestId;
    envelope.clientConnectionId = clientConnectionId;
    envelope.innerMessageId = packet.header.messageId;
    envelope.innerPayload = packet.payload;
    Packet forward;
    forward.header.messageId = static_cast<std::uint16_t>(MessageId::GatewayAccountForward);
    if (!account::EncodeAccountEnvelope(envelope, forward.payload)) {
        {
            std::lock_guard<std::mutex> lock(m_mapsMutex);
            m_pendingAccounts.erase(requestId);
        }
        session->EndAccount(packet.header.messageId);
        return;
    }
    m_loginConnection->Send(forward);
}

void GatewayServer::HandleAccountResponse(const Packet& packet) {
    AccountEnvelope envelope;
    std::string decodeError;
    if (!account::DecodeAccountEnvelope(packet.payload.data(), packet.payload.size(), envelope,
                                        decodeError)) {
        LOG_WARN("[Gateway] Malformed account response dropped: " + decodeError);
        return;
    }
    std::shared_ptr<GatewaySession> session;
    std::uint16_t innerMessageId = 0;
    std::uint64_t clientConnectionId = 0;
    {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
        auto it = m_pendingAccounts.find(envelope.requestId);
        if (it == m_pendingAccounts.end()) {
            return; // 超时已清理 / Client 已断开 -> 丢弃
        }
        innerMessageId = it->second.innerMessageId;
        clientConnectionId = it->second.clientConnectionId;
        m_pendingAccounts.erase(it);
        if (auto sit = m_sessions.find(clientConnectionId); sit != m_sessions.end()) {
            session = sit->second;
        }
    }
    if (session) {
        session->EndAccount(innerMessageId);
        Packet inner;
        inner.header.messageId = envelope.innerMessageId;
        inner.payload = std::move(envelope.innerPayload);
        session->SendPacket(inner);
    }
}

void GatewayServer::SendAccountErrorResponse(std::uint64_t clientConnectionId,
                                             std::uint64_t requestId,
                                             std::uint16_t innerMessageId,
                                             AccountErrorCode errorCode,
                                             const std::string& message) {
    // 合成对应 Response 类型的失败响应（成功路径由 LoginServer 生成）
    std::vector<std::uint8_t> payload;
    switch (static_cast<MessageId>(innerMessageId)) {
        case MessageId::RegisterResponse: {
            account::RegisterResponsePayload out;
            out.requestId = requestId;
            out.errorCode = static_cast<std::uint16_t>(errorCode);
            out.message = message;
            account::EncodeRegisterResponse(out, payload);
            break;
        }
        case MessageId::AccountLoginResponse: {
            account::AccountLoginResponsePayload out;
            out.requestId = requestId;
            out.errorCode = static_cast<std::uint16_t>(errorCode);
            out.message = message;
            account::EncodeAccountLoginResponse(out, payload);
            break;
        }
        case MessageId::SessionResumeResponse: {
            account::SessionResumeResponsePayload out;
            out.requestId = requestId;
            out.errorCode = static_cast<std::uint16_t>(errorCode);
            out.message = message;
            account::EncodeSessionResumeResponse(out, payload);
            break;
        }
        case MessageId::CharacterListResponse: {
            account::CharacterListResponsePayload out;
            out.requestId = requestId;
            out.errorCode = static_cast<std::uint16_t>(errorCode);
            out.message = message;
            account::EncodeCharacterListResponse(out, payload);
            break;
        }
        case MessageId::CharacterCreateResponse: {
            account::CharacterCreateResponsePayload out;
            out.requestId = requestId;
            out.errorCode = static_cast<std::uint16_t>(errorCode);
            out.message = message;
            account::EncodeCharacterCreateResponse(out, payload);
            break;
        }
        case MessageId::CharacterDeleteResponse: {
            account::CharacterDeleteResponsePayload out;
            out.requestId = requestId;
            out.errorCode = static_cast<std::uint16_t>(errorCode);
            out.message = message;
            account::EncodeCharacterDeleteResponse(out, payload);
            break;
        }
        case MessageId::CharacterSelectResponse: {
            account::CharacterSelectResponsePayload out;
            out.requestId = requestId;
            out.errorCode = static_cast<std::uint16_t>(errorCode);
            out.message = message;
            account::EncodeCharacterSelectResponse(out, payload);
            break;
        }
        default:
            return; // 未知类型不合成
    }
    Packet packet;
    packet.header.messageId = innerMessageId;
    packet.payload = std::move(payload);
    SendToClient(clientConnectionId, packet);
}

void GatewayServer::ForwardLogin(std::uint64_t clientConnectionId, const std::string& username,
                                 const std::string& token) {
    if (!m_loginAvailable.load() || !m_loginConnection) {
        // 指令四十九 + 阶段9.3指令七：ServiceUnavailable 单发（EncodeLoginResponse + 错误码）
        SendLoginError(clientConnectionId, LoginErrorCode::ServiceUnavailable,
                       "login service unavailable");
        std::shared_ptr<GatewaySession> session;
        {
            std::lock_guard<std::mutex> lock(m_mapsMutex);
            if (auto it = m_sessions.find(clientConnectionId); it != m_sessions.end()) {
                session = it->second;
            }
        }
        if (session) {
            session->CompleteLogin(false, 0, "", "login service unavailable");
        }
        return;
    }
    const std::uint64_t requestId = m_nextRequestId++;
    {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
        PendingLogin pending;
        pending.requestId = requestId;
        pending.clientConnectionId = clientConnectionId;
        m_pendingLogins[requestId] = pending; // 指令五十九：requestId -> clientConnectionId
    }

    Packet forward;
    forward.header.messageId = static_cast<std::uint16_t>(MessageId::GatewayLoginForward);
    // 阶段9.3指令三：EncodeGatewayLoginForward（Shared Protocol）
    GatewayLoginForwardPayload payload;
    payload.requestId = requestId;
    payload.clientConnectionId = clientConnectionId;
    payload.username = username;
    payload.token = token; // 仅内部转发；日志不打印 token（指令五十二/八十九）
    if (!EncodeGatewayLoginForward(payload, forward.payload)) {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
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
    std::shared_ptr<GatewaySession> session;
    {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
        auto it = m_pendingLogins.find(response.requestId);
        if (it == m_pendingLogins.end()) {
            return; // 超时已清理/Client 已断开 -> 丢弃（指令六十/六十一）
        }
        const std::uint64_t clientConnectionId = response.clientConnectionId;
        m_pendingLogins.erase(it);
        if (auto sit = m_sessions.find(clientConnectionId); sit != m_sessions.end()) {
            session = sit->second;
        }
    }
    const std::uint64_t clientConnectionId = response.clientConnectionId;
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
    if (session) {
        session->CompleteLogin(response.success, response.accountId, response.displayName,
                               response.message);
    }
    if (m_hooks.onLoginResult) {
        m_hooks.onLoginResult(clientConnectionId, response.success, response.accountId,
                              response.displayName);
    }
}

void GatewayServer::SendToClient(std::uint64_t connectionId, const Packet& packet) {
    std::shared_ptr<GatewaySession> session;
    {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
        if (auto it = m_sessions.find(connectionId); it != m_sessions.end()) {
            session = it->second;
        }
    }
    if (session) {
        session->SendPacket(packet);
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
    std::vector<PendingLogin> expiredLogins;
    std::vector<std::pair<std::uint64_t, PendingAccount>> expiredAccounts;
    {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
        for (auto it = m_pendingLogins.begin(); it != m_pendingLogins.end();) {
            if (now - it->second.createdAt > timeout) {
                expiredLogins.push_back(it->second);
                it = m_pendingLogins.erase(it);
            } else {
                ++it;
            }
        }
        // 阶段10：Account pending 超时 -> 合成对应 Response 类型的失败响应（RequestTimeout）
        for (auto it = m_pendingAccounts.begin(); it != m_pendingAccounts.end();) {
            if (now - it->second.createdAt > timeout) {
                expiredAccounts.emplace_back(it->first, it->second);
                it = m_pendingAccounts.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const PendingLogin& pending : expiredLogins) {
        // 指令十四：Pending 超时 -> LoginResponse(success=false, Timeout,
        // "login timeout")；迟到的 LoginGatewayResponse 因 requestId 不存在被丢弃
        SendLoginError(pending.clientConnectionId, LoginErrorCode::Timeout, "login timeout");
        std::shared_ptr<GatewaySession> session;
        {
            std::lock_guard<std::mutex> lock(m_mapsMutex);
            if (auto sit = m_sessions.find(pending.clientConnectionId); sit != m_sessions.end()) {
                session = sit->second;
            }
        }
        if (session) {
            session->CompleteLogin(false, 0, "", "login timeout");
        }
    }
    for (const auto& [requestId, pending] : expiredAccounts) {
        SendAccountErrorResponse(pending.clientConnectionId, requestId,
                                 legend::network::AccountResponseMessageId(pending.innerMessageId),
                                 AccountErrorCode::RequestTimeout, "account request timeout");
        std::shared_ptr<GatewaySession> session;
        {
            std::lock_guard<std::mutex> lock(m_mapsMutex);
            if (auto sit = m_sessions.find(pending.clientConnectionId); sit != m_sessions.end()) {
                session = sit->second;
            }
        }
        if (session) {
            session->EndAccount(pending.innerMessageId);
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
    std::vector<std::shared_ptr<GatewaySession>> expired;
    {
        std::lock_guard<std::mutex> lock(m_mapsMutex);
        for (auto it = m_sessions.begin(); it != m_sessions.end();) {
            auto session = it->second;
            // LoginPending is managed separately by the pending timeout, avoiding a race
            if (session->State() == GatewaySessionState::LoginPending) {
                ++it;
                continue;
            }
            if (now - session->LastPacketTime() > idle) {
                expired.push_back(session);
                it = m_sessions.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& session : expired) {
        const std::uint64_t id = session->ConnectionId();
        session->Disconnect();
        LOG_INFO("[Gateway] Client #" + std::to_string(id) + " idle timeout, disconnected.");
        if (m_hooks.onClientClosed) {
            m_hooks.onClientClosed(id, "idle timeout");
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
