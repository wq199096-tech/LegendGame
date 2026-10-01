#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpServer.h"
#include "Server/LoginServer/Account/CharacterService.h"
#include "Server/LoginServer/Account/AccountService.h"
#include "Server/LoginServer/Account/DbWorker.h"
#include "Server/LoginServer/Account/Database/Database.h"
#include "Server/LoginServer/Account/SessionService.h"
#include "Shared/InternalProtocol/InternalProtocol.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace legend::db {

class DbServer : public std::enable_shared_from_this<DbServer> {
public:
    struct Config {
        std::uint16_t listenPort = 7500;
        std::string databasePath = "Data/legend_account.db";
        std::string serviceToken;
    };
    explicit DbServer(legend::net::NetworkService& service);
    Config& GetConfig() { return m_config; }

    // Stage25.6 服务器管理台：Db 只读统计快照（GUI 线程 Collect 时拷贝）
    struct DbStatsSnapshot {
        std::size_t connectionCount = 0;   // 内部服务连接数
        bool dbOpen = false;               // SQLite 状态
        bool workerRunning = false;        // DbWorker 状态
        std::uint64_t queries = 0;         // 查询数
        std::uint64_t writes = 0;          // 写入数
        std::uint64_t transactions = 0;    // 原子事务数
        std::uint64_t failures = 0;        // 失败数
        std::size_t queueLength = 0;       // DbWorker Queue 长度
        int schemaVersion = 0;             // Migration 版本（运行时读取）
        std::string databasePath;          // DB 路径
        std::uint64_t packetsReceived = 0;
        std::uint64_t packetsSent = 0;
    };
    DbStatsSnapshot CollectStats() const;

    bool Start(std::string& error);
    void Stop();
    legend::internal::ServiceHealth Health() const { return m_health.load(); }
    std::size_t ConnectionCount() const;

private:
    struct Link { legend::net::TcpConnectionPtr connection; bool authenticated = false; };
    void OnAccepted(legend::net::TcpConnectionPtr connection);
    void OnPacket(std::uint64_t connectionId, const legend::network::Packet& packet);
    void OnClosed(std::uint64_t connectionId);
    void HandleRequest(std::uint64_t connectionId, legend::internal::DbRequest request);
    legend::internal::DbResponse Execute(const legend::internal::DbRequest& request);
    void Send(std::uint64_t connectionId, const legend::network::Packet& packet);

    legend::net::NetworkService& m_service;
    Config m_config;
    std::shared_ptr<legend::net::TcpServer> m_server;
    mutable std::mutex m_mutex;
    std::map<std::uint64_t, Link> m_links;
    std::atomic<bool> m_stopped{true};
    std::atomic<legend::internal::ServiceHealth> m_health{legend::internal::ServiceHealth::Unavailable};
    std::atomic<std::uint64_t> m_revision{1};

    // Stage25.6 管理台埋点（DB/io 线程写，GUI 线程读）
    std::atomic<std::uint64_t> m_queries{0};
    std::atomic<std::uint64_t> m_writes{0};
    std::atomic<std::uint64_t> m_transactions{0};
    std::atomic<std::uint64_t> m_failures{0};
    std::atomic<int> m_schemaVersion{0};
    legend::account::Database m_database;
    legend::account::DbWorker m_worker;
    legend::account::SessionService m_sessionService;
    legend::account::CharacterService m_characterService;
    legend::account::AccountService m_accountService;
};

} // namespace legend::db
