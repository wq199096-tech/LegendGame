#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpServer.h"

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

    struct Config {
        std::uint16_t listenPort = 7100;          // 指令四十一：默认 127.0.0.1:7100
        std::string databasePath = "data/legend_account.db"; // 指令四
        std::int64_t sessionTtlSeconds = 24 * 60 * 60;       // 指令三十二：默认 24h
        std::int64_t lockoutSeconds = 60;                    // 指令二十九
        int maxFailedLogins = 5;                             // 指令二十九
        double ticketTtlSeconds = 60.0;                      // 指令五十一
    };

    bool Start(std::string& error);
    void Stop();
    Config& GetConfig() { return m_config; } // 测试可改端口/DB 路径（Start 前设置）
    std::size_t GatewayCount() const { return m_gateways.size(); }
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
    // 把响应信封发回 Gateway（io 线程内调用；Gateway 断开则丢弃）。
    void SendAccountResponse(std::uint64_t gatewayConnectionId, std::uint64_t requestId,
                             std::uint64_t clientConnectionId, std::uint16_t innerMessageId,
                             std::vector<std::uint8_t> innerPayload);

    legend::net::NetworkService& m_service;
    Config m_config;
    std::shared_ptr<legend::net::TcpServer> m_server;
    // 阶段10：m_gateways 由 io 线程（accept/packet/close 回调）与主线程（Stop）
    // 并发访问——加锁（Stop clear vs OnGatewayClosed erase 数据竞态修复）
    std::mutex m_gatewaysMutex;
    std::map<std::uint64_t, GatewayLink> m_gateways;
    std::atomic<bool> m_stopped{false};
    Hooks m_hooks;

    // 阶段10：SQLite + 账号/角色/Session 服务（连接只在 DB Worker 线程使用）。
    legend::account::Database m_database;
    legend::account::DbWorker m_dbWorker;
    legend::account::AccountService m_accountService;
    legend::account::SessionService m_sessionService;
    legend::account::CharacterService m_characterService;
    legend::account::TicketStore m_ticketStore;
};

} // namespace legend::login
