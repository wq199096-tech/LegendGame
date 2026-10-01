#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpClient.h"
#include "Shared/InternalProtocol/InternalProtocol.h"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace legend::server {

class PersistenceClient : public std::enable_shared_from_this<PersistenceClient> {
public:
    struct Config {
        std::string host = "127.0.0.1";
        std::uint16_t port = 7500;
        legend::internal::ServiceType serviceType = legend::internal::ServiceType::Unknown;
        std::string instanceId;
        std::string serviceToken;
        std::chrono::milliseconds requestTimeout{5000};
        std::chrono::seconds heartbeatInterval{5};
        std::chrono::seconds heartbeatTimeout{15};
    };
    using Callback = std::function<void(legend::internal::DbResponse)>;

    PersistenceClient(legend::net::NetworkService& service, Config config);
    void Start();
    void Stop();
    // 阶段25.5：Graceful Shutdown——阻塞等待在途请求清零（或超时）；Stop 前调用。
    void Drain(std::chrono::milliseconds timeout);
    std::uint64_t AsyncRequest(legend::internal::DbOperation operation,
                               std::vector<std::uint8_t> payload, Callback callback,
                               std::uint64_t expectedVersion = 0);
    bool IsAvailable() const { return m_available.load(); }
    std::size_t InFlightCount() const;

private:
    struct Pending {
        Callback callback;
        std::chrono::steady_clock::time_point deadline;
    };
    void Connect();
    void OnConnected(legend::net::TcpConnectionPtr connection);
    void OnPacket(const legend::network::Packet& packet);
    void OnClosed();
    void ScheduleReconnect();
    void ScheduleTick();
    void Tick();
    void FailAll(legend::internal::InternalErrorCode code, const std::string& message);

    legend::net::NetworkService& m_service;
    Config m_config;
    std::shared_ptr<legend::net::TcpClient> m_client;
    legend::net::TcpConnectionPtr m_connection;
    asio::steady_timer m_reconnectTimer;
    asio::steady_timer m_tickTimer;
    std::atomic<bool> m_stopped{true};
    std::atomic<bool> m_available{false};
    std::atomic<std::uint64_t> m_nextRequestId{1};
    std::uint32_t m_reconnectAttempt = 0;
    std::uint64_t m_heartbeatSequence = 0;
    std::chrono::steady_clock::time_point m_nextHeartbeat{};
    std::chrono::steady_clock::time_point m_lastHeartbeat{};
    mutable std::mutex m_mutex;
    std::map<std::uint64_t, Pending> m_pending;
};

} // namespace legend::server
