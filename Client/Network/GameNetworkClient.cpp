#include "Client/Network/GameNetworkClient.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::client {

using legend::network::ByteReader;
using legend::network::ByteWriter;
using legend::network::ClientHelloPayload;
using legend::network::DecodeHeartbeatPong;
using legend::network::DecodeLoginResponse;
using legend::network::DecodeServerHello;
using legend::network::DisconnectNoticePayload;
using legend::network::EncodeClientHello;
using legend::network::EncodeDisconnectNotice;
using legend::network::EncodeHeartbeatPing;
using legend::network::EncodeLoginRequest;
using legend::network::HeartbeatPingPayload;
using legend::network::HeartbeatPongPayload;
using legend::network::LoginRequestPayload;
using legend::network::LoginResponsePayload;
using legend::network::MessageId;
using legend::network::Packet;
using legend::network::ServerHelloPayload;
namespace net = legend::net;

GameNetworkClient::GameNetworkClient()
    : m_client(std::make_shared<net::TcpClient>(m_service)),
      m_heartbeatTimer(m_service.Io()) {}

GameNetworkClient::~GameNetworkClient() {
    Disconnect(false);
    m_service.Stop();
}

void GameNetworkClient::Connect(const std::string& host, std::uint16_t port) {
    if (m_state.load() == NetworkState::Connecting ||
        m_state.load() == NetworkState::Connected ||
        m_state.load() == NetworkState::Handshaking ||
        m_state.load() == NetworkState::Ready) {
        return;
    }
    m_config.gatewayHost = host;
    m_config.gatewayPort = port;
    m_service.Start(); // 网络线程：io_context.run（缺失则所有 async 永不执行）
    SetState(NetworkState::Connecting);
    auto self = shared_from_this();
    m_client->Connect(
        host, port,
        [this, self](net::TcpConnectionPtr connection) {
            OnTransportConnected(std::move(connection));
        },
        [this, self](const std::error_code& ec) {
            // 指令三十六/三十七：DNS 失败/超时 -> Failed 事件，不 Crash
            SetState(NetworkState::Failed, ec ? ec.message() : "connect failed");
            NetworkEvent event;
            event.type = NetworkEvent::Type::ConnectFailed;
            event.message = ec ? ec.message() : "connect failed";
            PushEvent(std::move(event));
        });
}

void GameNetworkClient::Disconnect(bool notifyServer) {
    if (notifyServer && m_connection && m_connection->IsConnected()) {
        // 指令六十七 + 阶段9.2指令一：EncodeDisconnectNotice reason=ClientQuit
        DisconnectNoticePayload notice;
        notice.reason = "ClientQuit";
        Packet out;
        out.header.messageId = static_cast<std::uint16_t>(MessageId::DisconnectNotice);
        if (EncodeDisconnectNotice(notice, out.payload)) {
            m_connection->Send(out); // 发送失败仍 Close
        }
    }
    m_heartbeatTimer.cancel();
    if (m_connection) {
        m_connection->Close();
        m_connection.reset();
    }
    m_authenticated.store(false);
    SetState(NetworkState::Disconnected);
}

void GameNetworkClient::SendLogin(const std::string& username, const std::string& token) {
    if (m_state.load() != NetworkState::Ready || !m_connection) {
        return;
    }
    // 阶段9.2指令一：EncodeLoginRequest
    LoginRequestPayload request;
    request.username = username;
    request.token = token;
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::LoginRequest);
    if (EncodeLoginRequest(request, out.payload)) {
        m_connection->Send(out);
    }
}

void GameNetworkClient::PollEvents(std::deque<NetworkEvent>& out) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    while (!m_events.empty()) {
        out.push_back(std::move(m_events.front()));
        m_events.pop_front();
    }
}

void GameNetworkClient::UpdateHeartbeat(float) {
    // 指令六十四：15 秒无 Pong -> ConnectionLost 主动断开（主线程检查，steady_clock）
    if (m_state.load() != NetworkState::Ready || !m_connection) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - m_lastPongTime).count();
    if (elapsed > m_config.heartbeatTimeoutSeconds) {
        SetState(NetworkState::Disconnected, "heartbeat timeout");
        NetworkEvent event;
        event.type = NetworkEvent::Type::HeartbeatTimeout;
        event.message = "no heartbeat pong";
        PushEvent(std::move(event));
        Disconnect(false);
    }
}

void GameNetworkClient::OnTransportConnected(net::TcpConnectionPtr connection) {
    m_connection = connection;
    SetState(NetworkState::Handshaking); // 指令四十三：连上即发 ClientHello
    connection->Start(
        [this](const Packet& packet) { OnPacket(packet); },
        [this, self = shared_from_this()](std::uint64_t, const std::error_code& ec) {
            m_heartbeatTimer.cancel();
            m_authenticated.store(false);
            SetState(NetworkState::Disconnected, ec ? ec.message() : "connection closed");
            NetworkEvent event;
            event.type = NetworkEvent::Type::Disconnected;
            event.message = ec ? ec.message() : "connection closed";
            PushEvent(std::move(event));
        });
    NetworkEvent event;
    event.type = NetworkEvent::Type::Connected;
    PushEvent(std::move(event));
    SendClientHello();
}

void GameNetworkClient::SendClientHello() {
    if (!m_connection) {
        return;
    }
    // 阶段9.2指令一：Shared Protocol 统一（EncodeClientHello）
    ClientHelloPayload hello;
    hello.protocolVersion = legend::network::kProtocolVersion;
    hello.clientBuild = m_config.clientBuild;
    hello.clientName = m_config.clientName;
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::ClientHello);
    if (EncodeClientHello(hello, out.payload)) {
        m_connection->Send(out);
    }
}

void GameNetworkClient::OnPacket(const Packet& packet) {
    switch (static_cast<MessageId>(packet.header.messageId)) {
        case MessageId::ServerHello: {
            // 阶段9.2指令一：DecodeServerHello
            ServerHelloPayload hello;
            std::string decodeError;
            if (!DecodeServerHello(packet.payload.data(), packet.payload.size(), hello,
                                   decodeError)) {
                SetState(NetworkState::Failed, "malformed ServerHello");
                NetworkEvent event;
                event.type = NetworkEvent::Type::HandshakeFailed;
                event.message = "malformed ServerHello";
                PushEvent(std::move(event));
                return;
            }
            if (!hello.accepted) {
                // 指令四十六：version mismatch -> HandshakeFailed
                SetState(NetworkState::Failed, hello.message);
                NetworkEvent event;
                event.type = NetworkEvent::Type::HandshakeFailed;
                event.message = hello.message;
                PushEvent(std::move(event));
                Disconnect(false);
                return;
            }
            m_serverConnectionId.store(hello.connectionId);
            m_lastPongTime = std::chrono::steady_clock::now();
            SetState(NetworkState::Ready);
            NetworkEvent event;
            event.type = NetworkEvent::Type::HandshakeSuccess;
            event.message = hello.serverName;
            PushEvent(std::move(event));
            ScheduleHeartbeat(); // 指令六十二：Ready 后心跳
            return;
        }
        case MessageId::HeartbeatPong: {
            // 阶段9.1指令二十：Pong 只有 pingSequence+serverTimeMs；RTT 用本地 lastPingTime
            ByteReader reader(packet.payload.data(), packet.payload.size());
            reader.ReadUInt32(); // pingSequence
            reader.ReadUInt64(); // serverTimeMs
            if (!reader.IsValid() || reader.Remaining() != 0) {
                return;
            }
            const auto now = std::chrono::steady_clock::now();
            // 指令六十三：RTT = now - 上次 Ping 发出时刻
            const float rtt = static_cast<float>(
                std::chrono::duration<double, std::milli>(now - m_lastPingTime).count());
            m_lastRttMs.store(rtt);
            m_lastPongTime = now;
            return;
        }
        case MessageId::LoginResponse: {
            // 阶段9.2指令一/四/五：DecodeLoginResponse（u64 accountId + errorCode）
            LoginResponsePayload response;
            std::string decodeError;
            if (!DecodeLoginResponse(packet.payload.data(), packet.payload.size(), response,
                                     decodeError)) {
                return;
            }
            if (response.success) {
                m_authenticated.store(true);
            }
            // 指令一百零七：登录失败不断网，TCP 保持
            NetworkEvent event;
            event.type = NetworkEvent::Type::LoginResponse;
            event.loginSuccess = response.success;
            event.accountId = response.accountId;
            event.errorCode = response.errorCode;
            event.displayName = response.displayName;
            event.message = response.message;
            PushEvent(std::move(event));
            return;
        }
        default: {
            // 阶段9.2指令十：未知 MessageId -> ProtocolError 事件 + 断开（不再静默忽略）
            NetworkEvent event;
            event.type = NetworkEvent::Type::ProtocolError;
            event.message = "unknown message id " + std::to_string(packet.header.messageId);
            PushEvent(std::move(event));
            Disconnect(false);
            return;
        }
    }
}

void GameNetworkClient::ScheduleHeartbeat() {
    if (m_heartbeatScheduled.exchange(true)) {
        return;
    }
    ScheduleHeartbeatImpl();
}

void GameNetworkClient::ScheduleHeartbeatImpl() {
    m_heartbeatTimer.expires_after(
        std::chrono::milliseconds(static_cast<int>(m_config.heartbeatIntervalSeconds * 1000)));
    auto self = shared_from_this();
    m_heartbeatTimer.async_wait([this, self](const std::error_code& ec) {
        m_heartbeatScheduled.store(false);
        if (ec || m_state.load() != NetworkState::Ready || !m_connection) {
            return;
        }
        SendHeartbeat();
        ScheduleHeartbeatImpl();
    });
}

void GameNetworkClient::SendHeartbeat() {
    if (!m_connection) {
        return;
    }
    // 阶段9.2指令一：EncodeHeartbeatPing
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

void GameNetworkClient::PushEvent(NetworkEvent event) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_events.push_back(std::move(event));
}

void GameNetworkClient::SetState(NetworkState state, const std::string& error) {
    m_state.store(state);
    if (!error.empty()) {
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_lastError = error;
    }
}

std::string GameNetworkClient::LastError() const {
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_lastError;
}

} // namespace legend::client
