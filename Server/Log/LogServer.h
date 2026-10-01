#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpServer.h"
#include "Shared/InternalProtocol/InternalProtocol.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace legend::logserver {

class LogServer : public std::enable_shared_from_this<LogServer> {
public:
    struct Config {
        std::uint16_t listenPort = 7600;
        std::string logRoot = "Logs/Services";
        std::string serviceToken;
        std::size_t maxQueueSize = 8192;
    };
    explicit LogServer(legend::net::NetworkService& service);
    ~LogServer();
    Config& GetConfig() { return m_config; }
    bool Start(std::string& error);
    void Stop();
    std::size_t QueuedCount() const;
    std::uint64_t DroppedCount() const { return m_dropped.load(); }
    bool Enqueue(legend::internal::LogEvent event);

private:
    struct Link { legend::net::TcpConnectionPtr connection; bool authenticated = false; };
    void OnAccepted(legend::net::TcpConnectionPtr connection);
    void OnPacket(std::uint64_t id, const legend::network::Packet& packet);
    void OnClosed(std::uint64_t id);
    void Send(std::uint64_t id, const legend::network::Packet& packet);
    void WriterLoop();
    bool WriteBatch(std::deque<legend::internal::LogEvent>& batch);

    legend::net::NetworkService& m_service;
    Config m_config;
    std::shared_ptr<legend::net::TcpServer> m_server;
    mutable std::mutex m_linksMutex;
    std::map<std::uint64_t, Link> m_links;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::deque<legend::internal::LogEvent> m_queue;
    std::thread m_writer;
    std::atomic<bool> m_stopped{true};
    std::atomic<std::uint64_t> m_dropped{0};
};

} // namespace legend::logserver
