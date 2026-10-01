#include "Server/Common/PersistenceClient.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Network/MessageId.h"

#include <algorithm>

namespace legend::server {

using namespace legend::internal;
using legend::network::MessageId;
using legend::network::Packet;

PersistenceClient::PersistenceClient(legend::net::NetworkService& service, Config config)
    : m_service(service), m_config(std::move(config)),
      m_client(std::make_shared<legend::net::TcpClient>(service)),
      m_reconnectTimer(service.Io()), m_tickTimer(service.Io()) {}

void PersistenceClient::Start() {
    if (!m_stopped.exchange(false)) return;
    m_reconnectAttempt = 0;
    Connect();
    ScheduleTick();
}

void PersistenceClient::Stop() {
    if (m_stopped.exchange(true)) return;
    m_reconnectTimer.cancel();
    m_tickTimer.cancel();
    m_client->Cancel();
    if (m_connection) m_connection->Close();
    m_connection.reset();
    m_available.store(false);
    FailAll(InternalErrorCode::ServiceUnavailable, "persistence client stopped");
}

std::size_t PersistenceClient::InFlightCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pending.size();
}

std::uint64_t PersistenceClient::AsyncRequest(DbOperation operation,
                                              std::vector<std::uint8_t> payload,
                                              Callback callback,
                                              std::uint64_t expectedVersion) {
    const auto requestId = m_nextRequestId.fetch_add(1);
    if (!m_available.load() || !m_connection) {
        DbResponse response{requestId, InternalErrorCode::DatabaseUnavailable, 0,
                            "database service unavailable", {}};
        m_service.Post([callback = std::move(callback), response = std::move(response)]() mutable {
            if (callback) callback(std::move(response));
        });
        return requestId;
    }
    DbRequest request{requestId, operation, expectedVersion, std::move(payload)};
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::InternalDbRequest);
    if (!EncodeDbRequest(request, packet.payload)) {
        DbResponse response{requestId, InternalErrorCode::InvalidRequest, 0,
                            "cannot encode database request", {}};
        m_service.Post([callback = std::move(callback), response = std::move(response)]() mutable {
            if (callback) callback(std::move(response));
        });
        return requestId;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.emplace(requestId, Pending{std::move(callback),
            std::chrono::steady_clock::now() + m_config.requestTimeout});
    }
    m_connection->Send(packet);
    return requestId;
}

void PersistenceClient::Connect() {
    if (m_stopped.load()) return;
    auto self = shared_from_this();
    m_client->Connect(m_config.host, m_config.port,
        [self](legend::net::TcpConnectionPtr connection) { self->OnConnected(std::move(connection)); },
        [self](const std::error_code&) { self->ScheduleReconnect(); });
}

void PersistenceClient::OnConnected(legend::net::TcpConnectionPtr connection) {
    if (m_stopped.load()) { connection->Close(); return; }
    m_connection = std::move(connection);
    auto self = shared_from_this();
    m_connection->Start([self](const Packet& packet) { self->OnPacket(packet); },
                        [self](std::uint64_t, const std::error_code&) { self->OnClosed(); });
    ServiceHandshake handshake;
    handshake.serviceType = m_config.serviceType;
    handshake.instanceId = m_config.instanceId;
    handshake.serviceToken = m_config.serviceToken;
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::InternalServiceHandshake);
    if (EncodeServiceHandshake(handshake, packet.payload)) m_connection->Send(packet);
}

void PersistenceClient::OnPacket(const Packet& packet) {
    if (static_cast<MessageId>(packet.header.messageId) == MessageId::InternalServiceHandshakeAck) {
        ServiceHandshakeAck ack;
        std::string error;
        if (!DecodeServiceHandshakeAck(packet.payload.data(), packet.payload.size(), ack, error) ||
            !ack.accepted) {
            if (m_connection) m_connection->Close();
            return;
        }
        m_available.store(true);
        m_reconnectAttempt = 0;
        m_lastHeartbeat = std::chrono::steady_clock::now();
        m_nextHeartbeat = std::chrono::steady_clock::now() + m_config.heartbeatInterval;
        LOG_INFO("[PersistenceClient] DbServer ready for " + m_config.instanceId);
        return;
    }
    if (static_cast<MessageId>(packet.header.messageId) == MessageId::InternalHeartbeat) {
        Heartbeat heartbeat; std::string error;
        if (DecodeHeartbeat(packet.payload.data(), packet.payload.size(), heartbeat, error))
            m_lastHeartbeat = std::chrono::steady_clock::now();
        return;
    }
    if (static_cast<MessageId>(packet.header.messageId) != MessageId::InternalDbResponse ||
        !m_available.load()) return;
    DbResponse response;
    std::string error;
    if (!DecodeDbResponse(packet.payload.data(), packet.payload.size(), response, error)) return;
    Callback callback;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_pending.find(response.requestId);
        if (it == m_pending.end()) return;
        callback = std::move(it->second.callback);
        m_pending.erase(it);
    }
    if (callback) callback(std::move(response));
}

void PersistenceClient::OnClosed() {
    m_connection.reset();
    m_available.store(false);
    FailAll(InternalErrorCode::DatabaseUnavailable, "database connection lost");
    ScheduleReconnect();
}

void PersistenceClient::ScheduleReconnect() {
    if (m_stopped.load()) return;
    m_available.store(false);
    const auto seconds = std::min<std::uint32_t>(30, 1u << std::min<std::uint32_t>(m_reconnectAttempt++, 5));
    m_reconnectTimer.expires_after(std::chrono::seconds(seconds));
    auto self = shared_from_this();
    m_reconnectTimer.async_wait([self](const std::error_code& ec) { if (!ec) self->Connect(); });
}

void PersistenceClient::ScheduleTick() {
    if (m_stopped.load()) return;
    m_tickTimer.expires_after(std::chrono::milliseconds(250));
    auto self = shared_from_this();
    m_tickTimer.async_wait([self](const std::error_code& ec) {
        if (!ec) { self->Tick(); self->ScheduleTick(); }
    });
}

void PersistenceClient::Tick() {
    const auto now = std::chrono::steady_clock::now();
    if (m_available.load() && m_connection &&
        now - m_lastHeartbeat > m_config.heartbeatTimeout) {
        LOG_WARN("[PersistenceClient] DbServer heartbeat timeout for " + m_config.instanceId);
        m_connection->Close();
        return;
    }
    std::vector<std::pair<Callback, DbResponse>> expired;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (it->second.deadline <= now) {
                DbResponse response{it->first, InternalErrorCode::Timeout, 0,
                                    "database request timeout", {}};
                expired.emplace_back(std::move(it->second.callback), std::move(response));
                it = m_pending.erase(it);
            } else ++it;
        }
    }
    for (auto& entry : expired) if (entry.first) entry.first(std::move(entry.second));
    if (m_available.load() && m_connection && now >= m_nextHeartbeat) {
        Heartbeat heartbeat{++m_heartbeatSequence, static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()),
            ServiceHealth::Healthy};
        Packet packet;
        packet.header.messageId = static_cast<std::uint16_t>(MessageId::InternalHeartbeat);
        if (EncodeHeartbeat(heartbeat, packet.payload)) m_connection->Send(packet);
        m_nextHeartbeat = now + m_config.heartbeatInterval;
    }
}

void PersistenceClient::FailAll(InternalErrorCode code, const std::string& message) {
    std::vector<std::pair<Callback, DbResponse>> pending;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, item] : m_pending)
            pending.emplace_back(std::move(item.callback), DbResponse{id, code, 0, message, {}});
        m_pending.clear();
    }
    for (auto& item : pending) if (item.first) item.first(std::move(item.second));
}

} // namespace legend::server
