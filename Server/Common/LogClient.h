#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpClient.h"
#include "Shared/InternalProtocol/InternalProtocol.h"

#include <asio.hpp>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace legend::server {

class LogClient : public std::enable_shared_from_this<LogClient> {
public:
    struct Config {
        std::string host = "127.0.0.1";
        std::uint16_t port = 7600;
        legend::internal::ServiceType serviceType = legend::internal::ServiceType::Unknown;
        std::string instanceId;
        std::string serviceToken;
        std::size_t maxBufferedEvents = 512;
    };

    LogClient(legend::net::NetworkService& service, Config config);
    void Start();
    void Stop();
    bool Emit(legend::internal::LogEvent event);
    bool IsAvailable() const { return m_available.load(); }
    std::uint64_t DroppedCount() const { return m_dropped.load(); }

private:
    void Connect();
    void OnConnected(legend::net::TcpConnectionPtr connection);
    void OnPacket(const legend::network::Packet& packet);
    void OnClosed();
    void ScheduleReconnect();
    void FlushBuffered();
    bool Send(const legend::internal::LogEvent& event);

    legend::net::NetworkService& m_service;
    Config m_config;
    std::shared_ptr<legend::net::TcpClient> m_client;
    legend::net::TcpConnectionPtr m_connection;
    asio::steady_timer m_reconnectTimer;
    std::atomic<bool> m_stopped{true};
    std::atomic<bool> m_available{false};
    std::atomic<std::uint64_t> m_dropped{0};
    std::uint32_t m_reconnectAttempt = 0;
    std::mutex m_mutex;
    std::deque<legend::internal::LogEvent> m_buffer;
};

} // namespace legend::server
