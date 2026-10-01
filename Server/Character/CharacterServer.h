#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpServer.h"
#include "Server/Common/PersistenceClient.h"
#include "Server/Common/LogClient.h"
#include "Server/LoginServer/Account/TicketStore.h"
#include "Shared/Account/AccountProtocol.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace legend::character {

class CharacterServer : public std::enable_shared_from_this<CharacterServer> {
public:
    struct Config {
        std::uint16_t listenPort = 7400;
        std::string dbHost = "127.0.0.1";
        std::uint16_t dbPort = 7500;
        std::string logHost = "127.0.0.1";
        std::uint16_t logPort = 7600;
        std::string serviceToken;
        double ticketTtlSeconds = 60.0;
        std::chrono::milliseconds dbTimeout{5000};
    };
    explicit CharacterServer(legend::net::NetworkService& service);
    Config& GetConfig() { return m_config; }
    bool Start(std::string& error);
    void Stop();
    bool IsPersistenceAvailable() const;
    std::size_t ConnectionCount() const;

private:
    struct Link { legend::net::TcpConnectionPtr connection; bool authenticated = false; };
    using AccountCallback = std::function<void(bool, std::uint64_t, std::uint16_t, std::string)>;
    void OnAccepted(legend::net::TcpConnectionPtr connection);
    void OnPacket(std::uint64_t connectionId, const legend::network::Packet& packet);
    void OnClosed(std::uint64_t connectionId);
    void HandleAccount(std::uint64_t connectionId, const legend::account::AccountEnvelope& envelope);
    void HandleTicket(std::uint64_t connectionId, const legend::network::Packet& packet);
    void ValidateSession(const std::string& token, AccountCallback callback);
    void SendAccount(std::uint64_t connectionId, std::uint64_t envelopeRequestId,
                     std::uint64_t clientConnectionId, std::uint16_t responseMessageId,
                     std::vector<std::uint8_t> payload);
    void Send(std::uint64_t connectionId, const legend::network::Packet& packet);
    void Audit(legend::internal::LogEventType type, std::uint64_t accountId,
               std::uint64_t characterId, std::string message);

    legend::net::NetworkService& m_service;
    Config m_config;
    std::shared_ptr<legend::net::TcpServer> m_server;
    std::shared_ptr<legend::server::PersistenceClient> m_persistence;
    std::shared_ptr<legend::server::LogClient> m_log;
    legend::account::TicketStore m_tickets;
    mutable std::mutex m_mutex;
    std::map<std::uint64_t, Link> m_links;
    std::atomic<bool> m_stopped{true};
};

} // namespace legend::character
