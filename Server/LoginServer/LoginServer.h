#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpServer.h"
#include "Server/Common/PersistenceClient.h"
#include "Shared/Account/AccountProtocol.h"

#include "Server/LoginServer/Account/AccountService.h"
#include "Server/LoginServer/Account/CharacterService.h"
#include "Server/LoginServer/Account/DbWorker.h"
#include "Server/LoginServer/Account/Database/Database.h"
#include "Server/LoginServer/Account/SessionService.h"
#include "Server/LoginServer/Account/TicketStore.h"
#include "Shared/Network/MessageId.h"
#include "Shared/Network/NetworkConstants.h"
#include "Shared/Network/Protocol.h" // 阶段9.2指令二：Shared Protocol 统一

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace legend::network {
struct Packet;
}

namespace legend::login {

// 阶段9 指令五十三/九十一：LoginServer——纯 Console（不依赖 SDL/OpenGL，
// 指令八十六）。临时认证：username=="test" && token=="dev_token" -> success
//（LegacyDevLogin，阶段10 保留兼容）。
// 阶段10 指令五十五：本进程负责 Register/Login/Session/Character*/SelectionTicket。
class LoginServer : public std::enable_shared_from_this<LoginServer> {
public:
    struct Hooks {
        std::function<void(bool connected)> onGatewayConnectionChanged;
        std::function<void(std::uint64_t requestId, const std::string& username, bool success,
                           std::uint64_t accountId)>
            onAuthResult;
        std::function<void(std::uint16_t innerMessageId, std::uint64_t accountId, bool success)>
            onAccountResult; // 阶段10：账号链路观测（不携带敏感字段）
    };

    explicit LoginServer(legend::net::NetworkService& service);

    // Stage25.6 服务器管理台：Login 只读统计快照（GUI 线程 Collect 时拷贝）
    struct LoginStatsSnapshot {
        std::size_t gatewayCount = 0;      // 登录连接（Gateway）
        std::uint64_t authRequests = 0;    // 认证请求
        std::uint64_t authSuccess = 0;     // 认证成功
        std::uint64_t authFail = 0;        // 认证失败
        std::uint64_t accountRequests = 0; // 账号链路请求（注册/登录/会话/角色）
        std::uint64_t sessionsCreated = 0; // Session 创建次数
        std::uint64_t ticketsIssued = 0;   // Ticket 签发次数
        std::uint64_t ticketsConsumed = 0; // Ticket 消费次数
        std::size_t ticketsLive = 0;       // 未消费 Ticket 数
        std::uint64_t packetsReceived = 0;
        std::uint64_t packetsSent = 0;
        bool dbRemote = false;             // true=DbServer RPC 模式 / false=本地 SQLite
        bool dbAvailable = false;          // DbServer 状态 / 本地 DB 打开状态
    };
    LoginStatsSnapshot CollectStats() const;

    struct Config {
        std::uint16_t listenPort = 7100;          // 指令四十一：默认 127.0.0.1:7100
        std::string databasePath = "data/legend_account.db"; // 指令四
        std::int64_t sessionTtlSeconds = 24 * 60 * 60;       // 指令三十二：默认 24h
        std::int64_t lockoutSeconds = 60;                    // 指令二十九
        int maxFailedLogins = 5;                             // 指令二十九
        double ticketTtlSeconds = 60.0;                      // 指令五十一
        std::string dbHost = "127.0.0.1";
        std::uint16_t dbPort = 0; // production sets 7500; zero preserves isolated legacy tests
        std::string serviceToken;
        std::chrono::milliseconds dbTimeout{5000};
    };

    bool Start(std::string& error);
    void Stop();
    Config& GetConfig() { return m_config; } // 测试可改端口/DB 路径（Start 前设置）
    std::size_t GatewayCount() const {
        std::lock_guard<std::mutex> lock(m_gatewaysMutex);
        return m_gateways.size();
    }
    void SetHooks(Hooks hooks) { m_hooks = std::move(hooks); }
    // 测试钩子：直接访问内存 TicketStore（Validate/Consume 单元测试）。
    legend::account::TicketStore& Tickets() { return m_ticketStore; }

private:
    struct GatewayLink {
        legend::net::TcpConnectionPtr connection;
        bool handshakeDone = false;
    };

    void OnGatewayAccepted(legend::net::TcpConnectionPtr connection);
    void OnGatewayPacket(std::uint64_t connectionId, const legend::network::Packet& packet);
    void OnGatewayClosed(std::uint64_t connectionId);
    void HandleAuthRequest(std::uint64_t gatewayConnectionId,
                           const legend::network::Packet& packet);
    // 阶段11 指令十二：SelectionTicket 一次性消费（WorldServer 内部服务协议）。
    void HandleConsumeSelectionTicket(std::uint64_t gatewayConnectionId,
                                      const legend::network::Packet& packet);
    // 阶段10：GatewayAccountForward 信封 -> DB Worker 任务 -> post 回 io 发响应。
    void HandleAccountForward(std::uint64_t gatewayConnectionId,
                              const legend::network::Packet& packet);
    void HandleRemoteAccountForward(std::uint64_t gatewayConnectionId,
                                    const legend::account::AccountEnvelope& envelope);
    // 把响应信封发回 Gateway（io 线程内调用；Gateway 断开则丢弃）。
    void SendAccountResponse(std::uint64_t gatewayConnectionId, std::uint64_t requestId,
                             std::uint64_t clientConnectionId, std::uint16_t innerMessageId,
                             std::vector<std::uint8_t> innerPayload);

    legend::net::NetworkService& m_service;
    Config m_config;
    std::shared_ptr<legend::net::TcpServer> m_server;
    // 阶段10：m_gateways 由 io 线程（accept/packet/close 回调）与主线程（Stop）
    // 并发访问——加锁（Stop clear vs OnGatewayClosed erase 数据竞态修复）
    mutable std::mutex m_gatewaysMutex;
    std::map<std::uint64_t, GatewayLink> m_gateways;
    std::atomic<bool> m_stopped{false};
    Hooks m_hooks;

    // Stage25.6 管理台埋点（原子计数，DB/io 线程写，GUI 线程读）
    std::atomic<std::uint64_t> m_authRequests{0};
    std::atomic<std::uint64_t> m_authSuccess{0};
    std::atomic<std::uint64_t> m_authFail{0};
    std::atomic<std::uint64_t> m_accountRequests{0};
    std::atomic<std::uint64_t> m_sessionsCreated{0};

    // 阶段10：SQLite + 账号/角色/Session 服务（连接只在 DB Worker 线程使用）。
    legend::account::Database m_database;
    legend::account::DbWorker m_dbWorker;
    legend::account::AccountService m_accountService;
    legend::account::SessionService m_sessionService;
    legend::account::CharacterService m_characterService;
    legend::account::TicketStore m_ticketStore;
    std::shared_ptr<legend::server::PersistenceClient> m_persistence;
};

} // namespace legend::login
