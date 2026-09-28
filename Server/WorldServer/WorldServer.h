#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpClient.h"
#include "Engine/Network/TcpServer.h"
#include "Server/LoginServer/Account/AccountRepository.h"
#include "Server/LoginServer/Account/Database/Database.h"
#include "Server/LoginServer/Account/DbWorker.h"
#include "Server/WorldServer/AOI/WorldSpatialGrid.h"
#include "Server/WorldServer/WorldManager.h"
#include "Server/WorldServer/WorldMapManager.h"
#include "Server/WorldServer/WorldSession.h"

#include "Shared/Network/MessageId.h"
#include "Shared/World/WorldError.h"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace legend::network {
struct Packet;
}

namespace legend::world {

// 阶段11 指令一~四：LegendWorldServer。
// 接受 Client 世界连接（7200）+ 内部连接 LoginServer（7100）消费 SelectionTicket
// + 加载角色 + PlayerSession + 权威位置 + 位置保存。
// 纯 Console，不链接 SDL/OpenGL/Renderer/GameScene（指令六十一/一百零九）。
class WorldServer : public std::enable_shared_from_this<WorldServer> {
public:
    struct Config {
        std::uint16_t listenPort = kWorldServerDefaultPort; // 指令三：默认 7200
        std::string loginHost = "127.0.0.1";
        std::uint16_t loginPort = kLoginServerDefaultPort;  // 7100
        std::string databasePath = "data/legend_account.db";
        double loginReconnectSeconds = 2.0;
        double ticketPendingTimeoutSeconds = 3.0;           // 指令七十四
        double clientIdleTimeoutSeconds = 20.0;             // 指令七十七
        int snapshotIntervalMs = 100;                        // 指令四十二
        double positionSaveIntervalSeconds = 30.0;           // 指令五十四
        // 阶段12 AOI（指令五~七/十五/二十七/二十九）
        int aoiTickMs = 200;                                 // 指令十五：AOI tick 200ms
        float aoiEnterRadius = kAoiEnterRadius;              // 指令六：600
        float aoiLeaveRadius = kAoiLeaveRadius;              // 指令七：700（滞回）
        std::size_t aoiVisibleLimit = kAoiVisibleLimit;      // 指令二十七：128
    };

    struct Hooks {
        std::function<void(bool connected)> onLoginConnectionChanged;
        std::function<void(std::uint64_t characterId, bool entered)> onPlayerChanged;
    };

    explicit WorldServer(legend::net::NetworkService& service);

    bool Start(std::string& error);
    void Stop();
    Config& GetConfig() { return m_config; }
    bool IsLoginConnected() const { return m_loginAvailable.load(); }
    std::size_t PlayerCount() const { return m_players.PlayerCount(); }
    void SetHooks(Hooks hooks) { m_hooks = std::move(hooks); }

private:
    struct PendingTicket {
        std::uint64_t clientConnectionId = 0;
        std::chrono::steady_clock::time_point createdAt{std::chrono::steady_clock::now()};
    };

    // Client 连接（io 线程）
    void OnClientAccepted(legend::net::TcpConnectionPtr connection);
    void OnClientPacket(std::uint64_t connectionId, const legend::network::Packet& packet);
    void OnClientClosed(std::uint64_t connectionId, const std::error_code& ec);

    // Login 内部链路
    void ConnectToLogin();
    void ScheduleLoginReconnect();
    void OnLoginConnected(legend::net::TcpConnectionPtr connection);
    void HandleLoginLinkClosed();
    void OnLoginPacket(std::uint64_t linkId, const legend::network::Packet& packet);
    void SendConsumeRequest(std::uint64_t clientConnectionId, std::uint64_t requestId,
                            const std::string& ticket);

    // EnterWorld 管线
    void HandleEnterWorldRequest(std::uint64_t connectionId,
                                 const legend::network::Packet& packet);
    void HandleConsumeResponse(const legend::network::Packet& packet);
    void SendEnterWorldError(std::uint64_t connectionId, std::uint64_t requestId,
                             WorldErrorCode errorCode, const std::string& message);
    void SendEnterWorldSuccess(std::uint64_t connectionId, std::uint64_t requestId,
                               const std::shared_ptr<PlayerSession>& player);

    // 世界循环
    void HandleMoveInput(std::uint64_t connectionId, const legend::network::Packet& packet);
    void ScheduleSnapshotTimer();
    void SendPositionSnapshots();
    void ScheduleSaveTimer();
    void SaveDirtyPositions();
    void ScheduleTicketTimeoutCheck();
    void CheckTicketTimeouts();
    void ScheduleClientIdleCheck();
    void CheckClientIdle();

    // 阶段12：AOI（指令九~二十一/五十~五十二/六十九/七十）
    void ScheduleAoiTick();
    void RunAoiTick();
    void InitializePlayerVisibility(const std::shared_ptr<PlayerSession>& player);
    void NotifyPlayerGoneToObservers(std::uint64_t characterId, PlayerDespawnReason reason);
    void SendPlayerSpawn(const std::shared_ptr<PlayerSession>& receiver,
                         const std::shared_ptr<PlayerSession>& target);
    void SendPlayerDespawn(const std::shared_ptr<PlayerSession>& receiver,
                           std::uint64_t targetCharacterId, PlayerDespawnReason reason);
    void SendRemoteBatches(const std::shared_ptr<PlayerSession>& player, std::uint64_t serverTime);
    void SendPacketToPlayer(const std::shared_ptr<PlayerSession>& player,
                            const legend::network::Packet& packet);

    // 位置保存（指令五十二/五十三/五十四/五十六）
    void SavePlayerPosition(const std::shared_ptr<PlayerSession>& player, bool touchLastPlayed);
    void SavePlayerPositionNow(std::uint64_t characterId, std::uint16_t mapId, float x, float y);

    legend::net::NetworkService& m_service;
    Config m_config;
    std::shared_ptr<legend::net::TcpServer> m_server;
    std::shared_ptr<legend::net::TcpClient> m_loginClient;
    legend::net::TcpConnectionPtr m_loginConnection;
    std::atomic<bool> m_loginAvailable{false};
    bool m_loginHandshakeDone = false; // io 线程内访问
    std::atomic<bool> m_stopped{false};
    std::atomic<bool> m_reconnectScheduled{false};

    std::mutex m_sessionsMutex; // Stop（主线程）与 io 回调互斥
    std::map<std::uint64_t, std::shared_ptr<WorldSession>> m_sessions;
    std::map<std::uint64_t, PendingTicket> m_pendingTickets; // requestId -> pending

    WorldManager m_players;
    WorldMapManager m_mapManager;
    WorldSpatialGrid m_spatialGrid; // 阶段12 指令九：仅 io 线程访问（无锁）

    // 阶段11 指令二十五：World 独立 DB Worker（网络线程禁止直接 SQLite IO）
    legend::account::Database m_database;
    legend::account::DbWorker m_dbWorker;

    asio::steady_timer m_reconnectTimer;
    asio::steady_timer m_ticketTimer;
    asio::steady_timer m_snapshotTimer;
    asio::steady_timer m_saveTimer;
    asio::steady_timer m_idleTimer;
    asio::steady_timer m_aoiTimer; // 阶段12 指令六十九：AOI tick 200ms，Stop 时 cancel

    std::uint64_t m_nextRequestId = 1; // 指令七十三：单调增长
    Hooks m_hooks;
};

} // namespace legend::world
