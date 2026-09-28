#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpClient.h"
#include "Engine/Network/TcpServer.h"
#include "Server/LoginServer/Account/AccountRepository.h"
#include "Server/LoginServer/Account/Database/Database.h"
#include "Server/LoginServer/Account/DbWorker.h"
#include "Server/WorldServer/AOI/WorldSpatialGrid.h"
#include "Server/WorldServer/Monster/MonsterAi.h"
#include "Server/WorldServer/Monster/MonsterManager.h"
#include "Server/WorldServer/Monster/MonsterSpatialGrid.h"
#include "Server/WorldServer/Skill/SkillRegistry.h"
#include "Server/WorldServer/Skill/SkillService.h"
#include "Server/WorldServer/Status/StatusEffectRegistry.h"
#include "Server/WorldServer/Status/StatusEffectService.h"
#include "Server/WorldServer/WorldManager.h"
#include "Server/WorldServer/WorldMapManager.h"
#include "Server/WorldServer/WorldSession.h"

#include "Shared/Combat/CombatProtocol.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/Network/MessageId.h"
#include "Shared/Skill/SkillProtocol.h"
#include "Shared/Skill/SkillTypes.h"
#include "Shared/Status/StatusEffectProtocol.h"
#include "Shared/Status/StatusEffectTypes.h"
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
#include <vector>

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
        // 阶段13：怪物 AI（指令三十/六十九）
        int monsterAiTickMs = kMonsterAiTickMs;               // 200ms
        // 阶段14 指令六十八：Health Snapshot 纠偏（1s，单条）
        int healthSnapshotIntervalMs = kHealthSnapshotIntervalMs;
        // 阶段15 指令七十七/七十八：Skill Tick 50ms（与 AI Tick 200ms 分离，指令八十）
        int skillTickMs = 50;
        // 阶段15 指令六十七：ManaSnapshot 周期（1s，发给玩家本人）
        int manaSnapshotIntervalMs = 1000;
        // 阶段16 指令四十：Status Tick 100ms（统一扫描，不每状态一个 Timer）
        int statusTickMs = 100;
        // 阶段16 指令五十九：状态 Snapshot 纠偏周期（2s，只发可见实体）
        int statusSnapshotIntervalMs = 2000;
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

    // 阶段13：怪物访问器（测试/运维用；MonsterManager 内部互斥）。
    std::size_t MonsterCount() const { return m_monsters.Count(); }
    std::vector<std::uint64_t> MonsterEntityIds() const;
    std::shared_ptr<MonsterEntity> FindMonster(std::uint64_t entityId) const {
        return m_monsters.FindMonster(entityId);
    }
    // 阶段13 指令一百零六：移除怪物 -> 可见玩家收到 MonsterDespawn(Removed)。
    bool RemoveMonster(std::uint64_t entityId);
    // 阶段14：玩家访问器（测试用）。
    std::shared_ptr<PlayerSession> FindPlayerByCharacter(std::uint64_t characterId) const {
        return m_players.FindByCharacter(characterId);
    }
    // 阶段15：施法中的玩家数（测试/运维用）。
    std::size_t CastingPlayerCount() const;
    // 阶段15：测试辅助——移动怪物（含 SpatialGrid cell 更新；测试确定性布景用）。
    bool MoveMonsterTo(std::uint64_t entityId, float x, float y);

    // 阶段16：状态效果（指令十六~八十三）
    // 施加（技能命中/测试白盒共用；Client 不能直接施加，指令十七）。
    StatusApplyOutcome ApplyStatusToTarget(std::uint8_t targetType, std::uint64_t targetEntityId,
                                           StatusEffectId effectId, std::uint8_t stacks,
                                           std::uint8_t sourceType, std::uint64_t sourceEntityId,
                                           std::uint32_t sourceSkillId);
    // 技能命中后的状态施加（指令六十九：SkillImpact -> CombatEvent -> Status）。
    void ApplySkillStatus(const std::shared_ptr<PlayerSession>& caster,
                          const SkillDefinition& skill, std::uint8_t targetType,
                          std::uint64_t targetEntityId,
                          const std::vector<std::uint64_t>& combatReceivers);
    void ScheduleStatusTick();
    void RunStatusTick();
    void TickStatusContainer(StatusEffectContainer& container, CombatEntityType targetType,
                             std::uint64_t targetEntityId,
                             const std::function<void(const CombatEventPayload&,
                                                      const std::vector<std::uint64_t>&)>& onDamage,
                             const std::function<void(StatusEffectId, std::uint64_t)>& onKilled,
                             std::chrono::steady_clock::time_point now);
    void ScheduleStatusSnapshotTick();
    void SendStatusSnapshots();
    void SendStatusSnapshotFor(const std::shared_ptr<PlayerSession>& receiver,
                               CombatEntityType targetType, std::uint64_t targetEntityId,
                               const StatusEffectContainer& container,
                               std::chrono::steady_clock::time_point now);
    void SendStatusApplied(const std::vector<std::uint64_t>& receivers,
                           const ActiveStatusEffect& effect, std::uint32_t durationMs);
    void SendStatusUpdated(const std::vector<std::uint64_t>& receivers,
                           const ActiveStatusEffect& effect);
    void SendStatusRemoved(const std::vector<std::uint64_t>& receivers,
                           const ActiveStatusEffect& effect, StatusRemovedReason reason);
    void ClearStatusOnDeath(CombatEntityType targetType, std::uint64_t targetEntityId,
                            StatusEffectContainer& container,
                            const std::vector<std::uint64_t>& receivers);
    std::vector<std::uint64_t> MonsterStatusReceivers(std::uint64_t monsterEntityId,
                                                      std::uint64_t sourceCharacterId) const;
    std::vector<std::uint64_t> PlayerStatusReceivers(std::uint64_t characterId) const;
    std::vector<std::uint64_t> MonsterOrPlayerStatusReceivers(std::uint8_t targetType,
                                                              std::uint64_t targetEntityId,
                                                              std::uint8_t sourceType,
                                                              std::uint64_t sourceEntityId);
    void RecalculateTargetDerivedStats(CombatEntityType targetType, std::uint64_t targetEntityId);
    void SendStatusRemovedToPayload(const std::vector<std::uint64_t>& receivers,
                                    const StatusEffectRemovedPayload& payload);

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

    // 阶段13：怪物（指令十五/三十/三十一/六十五/六十九/七十/一百零六）
    void SpawnInitialMonsters();
    void ScheduleMonsterAiTick();
    void RunMonsterAiTick();
    void UpdatePlayerMonsterVisibility(const std::shared_ptr<PlayerSession>& player,
                                       bool initialVisibility);
    void NotifyMonsterGoneToObservers(std::uint64_t monsterEntityId, MonsterDespawnReason reason);
    void SendMonsterSpawn(const std::shared_ptr<PlayerSession>& receiver,
                          const std::shared_ptr<MonsterEntity>& monster);
    void SendMonsterDespawn(const std::shared_ptr<PlayerSession>& receiver,
                            std::uint64_t monsterEntityId, MonsterDespawnReason reason);
    void SendMonsterBatches(const std::shared_ptr<PlayerSession>& player, std::uint64_t serverTime);
    void OnTargetPlayerRemoved(std::uint64_t characterId);

    // 阶段14：服务器权威战斗（指令三十五/三十九/四十五/六十八/七十七）
    void HandlePlayerAttack(std::uint64_t connectionId, const legend::network::Packet& packet);
    void SendAttackResponse(const std::shared_ptr<PlayerSession>& player, std::uint64_t requestId,
                            bool success, CombatResultCode code, std::uint64_t targetEntityId);
    void BroadcastCombatEvent(const CombatEventPayload& event,
                              const std::vector<std::uint64_t>& receiverCharacterIds);
    void KillMonster(const std::shared_ptr<MonsterEntity>& monster, std::uint64_t killerCharacterId,
                     const std::vector<std::uint64_t>& observers);
    void TryMonsterAttack(const std::shared_ptr<MonsterEntity>& monster,
                          const MonsterDefinition& definition);
    void KillPlayer(const std::shared_ptr<PlayerSession>& victim, CombatEntityType killerType,
                    std::uint64_t killerId, const std::vector<std::uint64_t>& observers);
    void CleanupDeadMonsters();
    void ScheduleHealthSnapshotTick();
    void SendHealthSnapshots();
    void SendEntityHealthSnapshot(const std::shared_ptr<PlayerSession>& receiver,
                                  CombatEntityType entityType, std::uint64_t entityId,
                                  std::uint32_t currentHp, std::uint32_t maxHp, bool alive);

    // 阶段15：技能（指令三十三/三十五~四十六/七十一~八十九/一百二十一）
    void HandleSkillCastRequest(std::uint64_t connectionId, const legend::network::Packet& packet);
    void ExecuteInstantCast(const std::shared_ptr<PlayerSession>& caster,
                            const SkillDefinition& skill, std::uint64_t castId,
                            std::uint64_t targetEntityId);
    void BeginTimedCast(const std::shared_ptr<PlayerSession>& caster,
                        const SkillDefinition& skill, std::uint64_t castId,
                        std::uint64_t targetEntityId, std::uint64_t requestId);
    void CompleteCast(const std::shared_ptr<PlayerSession>& caster, PendingSkillCast& cast);
    void CancelActiveCast(const std::shared_ptr<PlayerSession>& caster,
                          SkillCancelReason reason);
    void ResolveAndApplySkillDamage(const std::shared_ptr<PlayerSession>& caster,
                                    const SkillDefinition& skill, std::uint64_t castId,
                                    std::uint64_t targetEntityId, bool hasTargetEntity);
    void ScheduleSkillTick();
    void RunSkillTick();
    void ScheduleManaSnapshotTick();
    void SendManaSnapshots();
    void SendSkillCastResponse(const std::shared_ptr<PlayerSession>& player,
                               std::uint64_t requestId, SkillId skillId, bool accepted,
                               SkillResultCode code);
    void SendSkillCastStarted(const std::vector<std::uint64_t>& receivers,
                              std::uint64_t castId, std::uint64_t casterCharacterId,
                              SkillId skillId, std::uint8_t targetType,
                              std::uint64_t targetEntityId, std::uint32_t castTimeMs);
    void SendSkillCastCompleted(const std::vector<std::uint64_t>& receivers,
                                std::uint64_t castId, std::uint64_t casterCharacterId,
                                SkillId skillId, std::uint8_t targetType,
                                std::uint64_t targetEntityId);
    void SendSkillCastCancelled(const std::vector<std::uint64_t>& receivers,
                                std::uint64_t castId, std::uint64_t casterCharacterId,
                                SkillId skillId, SkillCancelReason reason);
    void SendSkillImpactEvent(const std::vector<std::uint64_t>& receivers,
                              const SkillImpactEventPayload& impact);
    // 阶段15：收集"能看到 caster 的玩家 ∪ caster"（Started/Completed/Cancelled）
    std::vector<std::uint64_t> CasterObservers(std::uint64_t casterCharacterId) const;
    // 阶段15 指令一百二十九：合并去重接收者。
    static void MergeReceiver(std::vector<std::uint64_t>& receivers, std::uint64_t characterId);

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
    // 阶段13：怪物系统（runtime only，指令六十六：不进数据库）
    MonsterManager m_monsters;
    MonsterSpatialGrid m_monsterGrid;
    std::uint64_t m_nextMonsterEntityId = 1; // 指令七：单调计数器

    // 阶段14：战斗（指令十三：eventId 单调；runtime only 不持久化，指令八十）
    std::uint64_t m_nextCombatEventId = 1;

    // 阶段15：技能（指令二十六：castId 单调，不能用 Client requestId；
    // 指令三十三：SkillRegistry/SkillService 拆分；runtime only 不持久化，
    // 指令一百四十七：重启 Mana 恢复 100、CD 清空、Pending Cast 消失）
    SkillRegistry m_skillRegistry;
    std::uint64_t m_nextCastId = 1;

    // 阶段16：状态效果（指令十三：instanceId 单调；runtime-only 指令七十九）
    StatusEffectRegistry m_statusRegistry;
    std::uint64_t m_nextStatusInstanceId = 1;
    StatusEffectContainer m_detachedStatusContainer; // 目标已失效时 ApplyEffect 的占位容器

    // 阶段11 指令二十五：World 独立 DB Worker（网络线程禁止直接 SQLite IO）
    legend::account::Database m_database;
    legend::account::DbWorker m_dbWorker;

    asio::steady_timer m_reconnectTimer;
    asio::steady_timer m_ticketTimer;
    asio::steady_timer m_snapshotTimer;
    asio::steady_timer m_saveTimer;
    asio::steady_timer m_idleTimer;
    asio::steady_timer m_aoiTimer;        // 阶段12 指令六十九：AOI tick 200ms，Stop 时 cancel
    asio::steady_timer m_monsterAiTimer;  // 阶段13 指令三十：AI tick 200ms，Stop 时 cancel
    asio::steady_timer m_healthTimer;     // 阶段14 指令六十八：1s HP 纠偏，Stop 时 cancel
    asio::steady_timer m_skillTimer;      // 阶段15 指令七十八：Skill Tick 50ms，Stop 时 cancel
    asio::steady_timer m_manaTimer;       // 阶段15 指令六十七：1s Mana 快照，Stop 时 cancel
    asio::steady_timer m_statusTimer;     // 阶段16 指令四十：Status Tick 100ms，Stop 时 cancel
    asio::steady_timer m_statusSnapshotTimer; // 阶段16 指令五十九：2s 状态快照，Stop 时 cancel

    std::uint64_t m_nextRequestId = 1; // 指令七十三：单调增长
    Hooks m_hooks;
};

} // namespace legend::world
