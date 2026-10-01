#include "Server/Common/LogClient.h"

#include "Engine/Debug/Logger.h"
#include "Shared/Network/MessageId.h"

#include <algorithm>

namespace legend::server {

using namespace legend::internal;
using legend::network::MessageId;
using legend::network::Packet;

LogClient::LogClient(legend::net::NetworkService& service, Config config)
    : m_service(service), m_config(std::move(config)),
      m_client(std::make_shared<legend::net::TcpClient>(service)),
      m_reconnectTimer(service.Io()) {}

void LogClient::Start() {
    if (!m_stopped.exchange(false)) return;
    Connect();
}

void LogClient::Stop() {
    if (m_stopped.exchange(true)) return;
    m_reconnectTimer.cancel();
    m_client->Cancel();
    if (m_connection) m_connection->Close();
    m_connection.reset();
    m_available.store(false);
}

bool LogClient::Emit(LogEvent event) {
    if (ContainsSensitiveLogField(event.message, event.extraJson) ||
        !IsValidServiceType(event.service) || !IsValidLogEventType(event.eventType)) return false;
    if (m_available.load() && m_connection && Send(event)) return true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_buffer.size() >= m_config.maxBufferedEvents) {
            m_buffer.pop_front();
            m_dropped.fetch_add(1);
        }
        m_buffer.push_back(std::move(event));
    }
    LOG_WARN("[LogClient] remote log unavailable; event buffered locally for " +
             m_config.instanceId);
    return true;
}

void LogClient::Connect() {
    if (m_stopped.load()) return;
    auto self = shared_from_this();
    m_client->Connect(m_config.host, m_config.port,
        [self](legend::net::TcpConnectionPtr connection) { self->OnConnected(std::move(connection)); },
        [self](const std::error_code&) { self->ScheduleReconnect(); });
}

void LogClient::OnConnected(legend::net::TcpConnectionPtr connection) {
    if (m_stopped.load()) { connection->Close(); return; }
    m_connection = std::move(connection);
    auto self = shared_from_this();
    m_connection->Start([self](const Packet& packet) { self->OnPacket(packet); },
                        [self](std::uint64_t, const std::error_code&) { self->OnClosed(); });
    ServiceHandshake hello{m_config.serviceType, kInternalProtocolVersion,
                           m_config.instanceId, m_config.serviceToken};
    Packet packet; packet.header.messageId = static_cast<std::uint16_t>(MessageId::InternalServiceHandshake);
    if (EncodeServiceHandshake(hello, packet.payload)) m_connection->Send(packet);
}

void LogClient::OnPacket(const Packet& packet) {
    if (static_cast<MessageId>(packet.header.messageId) != MessageId::InternalServiceHandshakeAck) return;
    ServiceHandshakeAck ack; std::string error;
    if (!DecodeServiceHandshakeAck(packet.payload.data(), packet.payload.size(), ack, error) || !ack.accepted) {
        if (m_connection) m_connection->Close();
        return;
    }
    m_available.store(true);
    m_reconnectAttempt = 0;
    FlushBuffered();
}

void LogClient::OnClosed() {
    m_connection.reset();
    m_available.store(false);
    ScheduleReconnect();
}

void LogClient::ScheduleReconnect() {
    if (m_stopped.load()) return;
    const auto seconds = std::min<std::uint32_t>(30, 1u << std::min<std::uint32_t>(m_reconnectAttempt++, 5));
    m_reconnectTimer.expires_after(std::chrono::seconds(seconds));
    auto self = shared_from_this();
    m_reconnectTimer.async_wait([self](const std::error_code& ec) { if (!ec) self->Connect(); });
}

void LogClient::FlushBuffered() {
    std::deque<LogEvent> buffered;
    { std::lock_guard<std::mutex> lock(m_mutex); buffered.swap(m_buffer); }
    for (auto& event : buffered) {
        if (!Send(event)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_buffer.push_front(std::move(event));
            break;
        }
    }
}

bool LogClient::Send(const LogEvent& event) {
    if (!m_connection) return false;
    Packet packet; packet.header.messageId = static_cast<std::uint16_t>(MessageId::InternalLogEvent);
    if (!EncodeLogEvent(event, packet.payload)) return false;
    m_connection->Send(packet);
    return true;
}

} // namespace legend::server
