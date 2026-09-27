#include "Client/Network/GameNetworkClient.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::client {

using legend::network::ByteReader;
using legend::network::ByteWriter;
using legend::network::MessageId;
using legend::network::Packet;
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
        // 指令六十七：DisconnectNotice reason=ClientQuit，发送失败仍 Close
        Packet notice;
        notice.header.messageId = static_cast<std::uint16_t>(MessageId::DisconnectNotice);
        ByteWriter writer(notice.payload);
        if (writer.WriteString("ClientQuit")) {
            m_connection->Send(notice);
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
    Packet request;
    request.header.messageId = static_cast<std::uint16_t>(MessageId::LoginRequest);
    ByteWriter writer(request.payload);
    if (!writer.WriteString(username) || !writer.WriteString(token)) {
        return;
    }
    m_connection->Send(request);
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
    Packet hello;
    hello.header.messageId = static_cast<std::uint16_t>(MessageId::ClientHello);
    ByteWriter writer(hello.payload);
    // 指令四十四：version/build/name，不含密码
    writer.WriteUInt16(legend::network::kProtocolVersion);
    writer.WriteString(m_config.clientBuild);
    writer.WriteString(m_config.clientName);
    m_connection->Send(hello);
}

void GameNetworkClient::OnPacket(const Packet& packet) {
    switch (static_cast<MessageId>(packet.header.messageId)) {
        case MessageId::ServerHello: {
            ByteReader reader(packet.payload.data(), packet.payload.size());
            const bool accepted = reader.ReadBool();
            reader.ReadUInt16(); // serverProtocolVersion
            const std::uint64_t connectionId = reader.ReadUInt64();
            std::string serverName;
            std::string message;
            if (!reader.IsValid() || !reader.ReadString(serverName) ||
                !reader.ReadString(message)) {
                SetState(NetworkState::Failed, "malformed ServerHello");
                NetworkEvent event;
                event.type = NetworkEvent::Type::HandshakeFailed;
                event.message = "malformed ServerHello";
                PushEvent(std::move(event));
                return;
            }
            if (!accepted) {
                // 指令四十六：version mismatch -> HandshakeFailed
                SetState(NetworkState::Failed, message);
                NetworkEvent event;
                event.type = NetworkEvent::Type::HandshakeFailed;
                event.message = message;
                PushEvent(std::move(event));
                Disconnect(false);
                return;
            }
            m_serverConnectionId.store(connectionId);
            m_lastPongTime = std::chrono::steady_clock::now();
            SetState(NetworkState::Ready);
            NetworkEvent event;
            event.type = NetworkEvent::Type::HandshakeSuccess;
            event.message = serverName;
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
            ByteReader reader(packet.payload.data(), packet.payload.size());
            const bool success = reader.ReadBool();
            const std::uint32_t accountId = reader.ReadUInt32();
            std::string displayName;
            std::string message;
            if (!reader.IsValid() || !reader.ReadString(displayName) ||
                !reader.ReadString(message)) {
                return;
            }
            if (success) {
                m_authenticated.store(true);
            }
            // 指令一百零七：登录失败不断网，TCP 保持
            NetworkEvent event;
            event.type = NetworkEvent::Type::LoginResponse;
            event.loginSuccess = success;
            event.accountId = accountId;
            event.displayName = displayName;
            event.message = message;
            PushEvent(std::move(event));
            return;
        }
        default:
            return; // Client 侧未知消息忽略（Server 侧按协议错误断开）
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
    Packet ping;
    ping.header.messageId = static_cast<std::uint16_t>(MessageId::HeartbeatPing);
    ByteWriter writer(ping.payload);
    writer.WriteUInt32(++m_pingSequence);
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    writer.WriteUInt64(
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count()));
    m_lastPingTime = std::chrono::steady_clock::now();
    m_connection->Send(ping);
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
