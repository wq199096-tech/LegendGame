#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpClient.h"
#include "Engine/Network/TcpServer.h"
#include "Server/Common/PersistenceClient.h"
#include "Server/LoginServer/Account/AccountRepository.h"
#include "Server/LoginServer/Account/Database/Database.h"
#include "Server/LoginServer/Account/DbWorker.h"
#include "Server/WorldServer/AOI/WorldSpatialGrid.h"
#include "Server/WorldServer/Item/DropRoller.h"
#include "Server/WorldServer/Item/InventoryRepository.h"
#include "Server/WorldServer/Item/ItemRegistry.h"
#include "Server/WorldServer/Item/WorldItemDrop.h"
#include "Server/WorldServer/Map/MapTransitionService.h"
#include "Server/WorldServer/Monster/MonsterAi.h"
#include "Server/WorldServer/Monster/MonsterManager.h"
#include "Server/WorldServer/Monster/MonsterRespawnManager.h"
#include "Server/WorldServer/Monster/MonsterSpatialGrid.h"
#include "Server/WorldServer/Npc/NpcManager.h"
#include "Server/WorldServer/Npc/NpcSpatialGrid.h"
#include "Server/WorldServer/Npc/NpcInteractionService.h"
#include "Server/WorldServer/Portal/PortalManager.h"
#include "Server/WorldServer/Portal/PortalSpatialGrid.h"
#include "Server/WorldServer/Progression/ProgressionService.h"
#include "Server/WorldServer/Progression/RewardService.h"
#include "Server/WorldServer/Quest/QuestRepository.h"
#include "Server/WorldServer/Quest/QuestService.h"
#include "Server/WorldServer/Skill/SkillRegistry.h"
#include "Server/WorldServer/Skill/SkillService.h"
#include "Server/WorldServer/Status/StatusEffectRegistry.h"
#include "Server/WorldServer/Status/StatusEffectService.h"
#include "Server/WorldServer/WorldManager.h"
#include "Server/WorldServer/WorldMapManager.h"
#include "Server/WorldServer/WorldSession.h"

#include "Shared/Combat/CombatProtocol.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/Dialogue/DialogueProtocol.h"
#include "Shared/Item/ItemProtocol.h"
#include "Shared/Item/ItemTypes.h"
#include "Shared/Network/MessageId.h"
#include "Shared/Npc/NpcError.h"
#include "Shared/Npc/NpcProtocol.h"
#include "Shared/Portal/PortalProtocol.h"
#include "Shared/Progression/ProgressionProtocol.h"
#include "Shared/Quest/QuestProtocol.h"
#include "Shared/Quest/QuestTypes.h"
#include "Shared/Shop/ShopDefinition.h"
#include "Shared/Shop/ShopProtocol.h"
#include "Shared/Shop/ShopTypes.h"
#include "Shared/Skill/SkillProtocol.h"
#include "Shared/Skill/SkillTypes.h"
#include "Shared/Status/StatusEffectProtocol.h"
#include "Shared/Status/StatusEffectTypes.h"
#include "Shared/Teleport/TeleportProtocol.h"
#include "Shared/Teleport/TeleportTypes.h"
#include "Shared/World/WorldError.h"
#include "Shared/WorldMap/MapProtocol.h"
#include "Shared/WorldMap/RespawnProtocol.h"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
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
    friend class MapTransitionService; // 阶段21 指令二十四：统一地图切换（访问内部编排）

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
        // 阶段17 指令二十四：Respawn Tick 250ms（统一轮询，不每怪一个 Timer）
        int respawnTickMs = 250;
        // 阶段17 指令二十二：Training Slime respawnDelay = 8s（从 MonsterDeath 起算）
        std::uint32_t respawnDelayMs = 8000;
        // 阶段17 指令十五：ProgressionSnapshot 纠偏周期（30s，发给本人）
        int progressionSnapshotIntervalMs = 30000;
        // 阶段18 指令四十三：Drop cleanup tick 500ms（统一轮询，不每 drop 一个 Timer）
        int itemDropTickMs = 500;
        // 阶段18 指令十五/十六/二十二：Owner lock 10s / TTL 60s / 拾取距离 100
        std::uint32_t itemOwnerLockMs = kItemOwnerLockMs;
        std::uint32_t itemDropTtlMs = kItemDropTtlMs;
        float itemPickupRange = kItemPickupRange;
        // 阶段18 指令十一：DropRoller 种子（0 = 随机）；测试固定 seed 可复现。
        std::uint64_t dropRollerSeed = 0;
        // 阶段18 测试专用：掉落表按 100% 掷骰（链路完全不变，仅概率确定性）。
        bool testForceDropAll = false;
        // 阶段19 指令四十七：QuestSnapshot 周期纠偏（10s，发给本人；测试可缩短）。
        int questSnapshotIntervalMs = 10000;
        // 阶段20 指令二十二/四十一：NPC Dialogue/Shop Session TTL（默认 30s；测试可缩短）。
        double npcSessionTtlSeconds = kNpcSessionTtlSeconds;
        // 阶段21 指令十一：Legacy Test Spawn——生产按新地图布局（Map1 无野外 Slime）；
        // 历史测试依赖 Map1 Slime（NPC/战斗/技能套件），测试环境开启此开关。
        bool legacyMap1TestSpawn = false;
        // 阶段22 22.11/22.12：世界数据目录（maps/npcs/monster_spawns/portals JSON）。
        // 存在 → 加载+校验（失败拒绝启动）；不存在 → 出厂默认（MakeDefaultWorldData）。
        std::string worldDataDir = "Data/World";
        // 阶段23 23.22：Game 数据目录（items/monsters/skills/statuses/quests/
        // shops/teleports/loot_tables JSON）；策略与 worldDataDir 一致。
        std::string gameDataDir = "Data/Game";
        // 阶段25.5：DbServer RPC 持久化（dbPort=0 保留 legacy 本地 DB——隔离测试用；
        // 正式部署经 servers.json 指向 DbServer，World io 线程零 SQLite）。
        std::string dbHost = "127.0.0.1";
        std::uint16_t dbPort = 0;
        std::string serviceToken;
        std::chrono::milliseconds dbTimeout{5000};
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
    // 阶段17：SpawnSlot/Respawn 测试访问器（MonsterRespawnManager 只读视图）。
    std::size_t SpawnSlotCount() const { return m_respawnManager.SlotCount(); }
    std::size_t RespawnPendingCount() const { return m_respawnManager.PendingCount(); }
    std::uint32_t SpawnSlotOfEntity(std::uint64_t entityId) const {
        return m_respawnManager.SlotOfEntity(entityId);
    }
    std::uint64_t ActiveEntityOfSlot(std::uint32_t spawnSlotId) const {
        return m_respawnManager.ActiveEntityOfSlot(spawnSlotId);
    }

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

    // 阶段17：成长/奖励/重生（服务器权威，Client 不能决定）
    // 指令七：击杀归属 = 最后造成致死伤害的 Player（Basic/Skill/DOT sourceEntityId）。
    void GrantMonsterReward(const std::shared_ptr<MonsterEntity>& monster,
                            std::uint64_t killerCharacterId);
    // 指令十三/十四/十五：奖励与成长事件发送。
    void SendRewardGranted(const std::shared_ptr<PlayerSession>& killer,
                           std::uint64_t sourceMonsterEntityId, std::uint32_t expGain,
                           std::uint32_t goldGain, std::int64_t newExp, std::int64_t newGold);
    void SendLevelUpEvent(const std::vector<std::uint64_t>& receivers,
                          const std::shared_ptr<PlayerSession>& player, std::uint32_t oldLevel,
                          std::uint32_t newLevel);
    void SendProgressionSnapshot(const std::shared_ptr<PlayerSession>& player);
    void ScheduleProgressionSnapshotTick();
    void SendProgressionSnapshots();
    // 指令二十三~二十八：Respawn 编排。
    void ScheduleRespawnTick();
    void RunRespawnTick();
    std::shared_ptr<MonsterEntity> SpawnMonsterAtSlot(const MonsterSpawnSlot& slot,
                                                      std::uint64_t entityId);

    // 阶段18：服务器权威掉落/背包/装备（Client 不能决定，指令一）
    // 指令三十九：MonsterDeath 时 GenerateLoot（在阶段17 Reward 之后）。
    void GenerateMonsterDrops(const std::shared_ptr<MonsterEntity>& monster,
                              std::uint64_t killerCharacterId);
    // 指令四十三：Drop cleanup tick（TTL 过期）。
    void ScheduleItemDropTick();
    void RunItemDropTick();
    // 指令十八：Drop AOI（Enter 600 / Leave 700，visibleItemDrops 权威维护）。
    void UpdatePlayerItemDropVisibility(const std::shared_ptr<PlayerSession>& player,
                                        bool initialVisibility);
    void NotifyItemDropGoneToObservers(std::uint64_t dropEntityId,
                                       ItemDespawnReason reason);
    void SendWorldItemSpawn(const std::shared_ptr<PlayerSession>& receiver,
                            const WorldItemDrop& drop);
    void SendWorldItemDespawn(const std::shared_ptr<PlayerSession>& receiver,
                              std::uint64_t dropEntityId, ItemDespawnReason reason);
    // 指令二十一~二十五：拾取校验/原子 claim/DB 事务/失败回滚。
    void HandleItemPickupRequest(std::uint64_t connectionId, const legend::network::Packet& packet);
    void SendItemPickupResponse(const std::shared_ptr<PlayerSession>& player,
                                std::uint64_t requestId, std::uint64_t dropEntityId, bool success,
                                ItemResultCode code);
    // 指令二十七/三十三/三十四：装备/卸下（原子 + Derived 统一重算）。
    void HandleEquipItemRequest(std::uint64_t connectionId, const legend::network::Packet& packet);
    void HandleUnequipItemRequest(std::uint64_t connectionId,
                                  const legend::network::Packet& packet);
    void SendEquipItemResponse(const std::shared_ptr<PlayerSession>& player,
                               std::uint64_t requestId, bool success, ItemResultCode code,
                               EquipmentSlot slot);
    void SendUnequipItemResponse(const std::shared_ptr<PlayerSession>& player,
                                 std::uint64_t requestId, bool success,
                                 ItemResultCode code, EquipmentSlot slot);
    // 指令二十八/二十九：Snapshot/Delta 同步（Client 只是镜像）。
    void SendInventorySnapshot(const std::shared_ptr<PlayerSession>& player);
    void SendInventoryDelta(const std::shared_ptr<PlayerSession>& player, std::uint8_t opcode,
                            const InventoryEntry& entry, std::uint32_t slotIndex);
    void SendEquipmentSnapshot(const std::shared_ptr<PlayerSession>& player);
    // 指令三十一/三十二：装备加成并入 Derived（统一 RecalculateDerivedStats 入口）。
    void RefreshEquipmentBonuses(const std::shared_ptr<PlayerSession>& player);
    // 进入世界加载持久化背包/装备（DB Worker 线程任务里调用，io 线程应用结果）。
    void ApplyLoadedItems(const std::shared_ptr<PlayerSession>& player,
                          const std::vector<InventoryRepository::InventoryRow>& rows);
    // 测试/运维访问器（白盒）。
    std::size_t WorldItemDropCount() const { return m_itemDrops.Count(); }
    const WorldItemDrop* FindItemDrop(std::uint64_t dropEntityId) const {
        return m_itemDrops.Find(dropEntityId);
    }
    std::uint64_t NextItemDropIdForTest() const { return m_nextItemDropId; }
    // 阶段18 测试布景辅助（全部经 m_service.Post 投递 io 线程，与游戏逻辑串行）。
    bool TestSpawnDrop(float x, float y, std::uint16_t mapId, std::uint64_t ownerCharacterId,
                       std::uint32_t definitionId);
    bool TestEraseVisibleItemDrop(std::uint64_t characterId, std::uint64_t dropEntityId);
    bool TestAddVisibleItemDrop(std::uint64_t characterId, std::uint64_t dropEntityId);
    bool TestFillInventory(std::uint64_t characterId);
    bool TestMarkPlayerDead(std::uint64_t characterId);
    bool TestRevivePlayer(std::uint64_t characterId);
    bool TestBuffPlayerHp(std::uint64_t characterId, std::uint32_t hp);

    // ------------------------------------------------------------------
    // 阶段19：服务器权威任务（100% WorldServer 权威，指令二；
    // Client 只能发 Accept/TurnIn/Abandon——指令二）。
    // ------------------------------------------------------------------
    // 请求入口（io 线程；校验链见指令二十/三十四/四十三）。
    void HandleQuestAcceptRequest(std::uint64_t connectionId, const legend::network::Packet& packet);
    void HandleQuestTurnInRequest(std::uint64_t connectionId, const legend::network::Packet& packet);
    void HandleQuestAbandonRequest(std::uint64_t connectionId,
                                   const legend::network::Packet& packet);
    // 响应/事件（全部只发本人——指令一百二十五~一百二十七）。
    void SendQuestAcceptResponse(const std::shared_ptr<PlayerSession>& player,
                                 std::uint64_t requestId, QuestId questId, bool success,
                                 QuestResultCode code);
    void SendQuestTurnInResponse(const std::shared_ptr<PlayerSession>& player,
                                 std::uint64_t requestId, QuestId questId, bool success,
                                 QuestResultCode code);
    void SendQuestAbandonResponse(const std::shared_ptr<PlayerSession>& player,
                                  std::uint64_t requestId, QuestId questId, bool success,
                                  QuestResultCode code);
    void SendQuestProgressUpdated(const std::shared_ptr<PlayerSession>& player, QuestId questId,
                                  std::uint32_t objectiveId, std::uint32_t current,
                                  std::uint32_t required, QuestState state);
    void SendQuestStateChanged(const std::shared_ptr<PlayerSession>& player, QuestId questId,
                               QuestState oldState, QuestState newState);
    // 指令四十五/四十七：Snapshot（进世界下发 + 每 10s 本人纠偏）。
    void SendQuestSnapshot(const std::shared_ptr<PlayerSession>& player);
    void ScheduleQuestSnapshotTick();
    void SendQuestSnapshots();
    // 事件钩子编排（QuestService 推进 -> DB 写 + 事件；指令二十八/五十三~五十七）。
    void HandleQuestObjectiveChanges(const std::shared_ptr<PlayerSession>& player,
                                     const std::vector<QuestService::ObjectiveChange>& changes,
                                     const std::vector<QuestService::StateChange>& stateChanges);
    void HandleQuestMonsterKilled(std::uint64_t killerCharacterId, std::uint32_t monsterTypeId);
    void HandleQuestInventoryChanged(const std::shared_ptr<PlayerSession>& player);
    void HandleQuestLevelChanged(const std::shared_ptr<PlayerSession>& player);
    void HandleQuestPlayerMoved(const std::shared_ptr<PlayerSession>& player);
    // 指令六十八：EnterWorld 加载持久化任务（DB Worker 任务里调用，io 线程应用）。
    void ApplyLoadedQuests(const std::shared_ptr<PlayerSession>& player,
                           const std::vector<QuestRepository::QuestRow>& questRows,
                           const std::vector<QuestRepository::ObjectiveRow>& objectiveRows);
    // 测试白盒：直接设置任务状态/进度（不写 DB；QuestLogFull 等布景用）。
    bool TestSeedQuestProgress(std::uint64_t characterId, QuestId questId,
                               const std::unordered_map<std::uint32_t, std::uint32_t>& progress,
                               QuestState state);
    bool TestAcceptQuest(std::uint64_t characterId, QuestId questId);

    // ------------------------------------------------------------------
    // 阶段20：NPC / Dialogue / Shop / Teleport（100% 服务器权威；Client 只表达
    // 意图——指令二）。
    // ------------------------------------------------------------------
    // NPC 访问器（测试/运维白盒）。
    std::size_t NpcCount() const { return m_npcs.Count(); }
    const NpcEntity* FindNpc(std::uint64_t npcEntityId) const { return m_npcs.Find(npcEntityId); }
    // 指令三十一/三十三：per-player Marker（状态变化/Spawn 后重算并发送）。
    void SendNpcQuestMarkersFor(const std::shared_ptr<PlayerSession>& player);
    // 指令六十五~六十七：服务器权威传送（Teleport Option 触发）。
    bool TestTeleportPlayer(std::uint64_t characterId, std::uint16_t mapId, float x, float y);

    // ------------------------------------------------------------------
    // 阶段21：多地图 / Portal / 复活（100% 服务器权威；Client 只表达意图）。
    // ------------------------------------------------------------------
    // Portal 访问器（测试/运维白盒）。
    std::size_t PortalCount() const { return m_portals.Count(); }
    const PortalDefinition* FindPortal(std::uint64_t portalEntityId) const {
        return m_portals.Find(portalEntityId);
    }
    // 测试白盒：设置等级（Portal 8003 Level 验证等布景用；不写 DB）。
    bool TestSetPlayerLevel(std::uint64_t characterId, std::uint32_t level);
    // 测试白盒：设置金币（Portal/复活 Gold 验证布景用；不写 DB，内存权威值）。
    bool TestSetPlayerGold(std::uint64_t characterId, std::int64_t gold);

private:
    // 阶段21：Portal 生成与 AOI。
    void SpawnInitialPortals();
    void UpdatePlayerPortalVisibility(const std::shared_ptr<PlayerSession>& player,
                                      bool initialVisibility);
    void SendPortalSpawn(const std::shared_ptr<PlayerSession>& receiver,
                         std::uint64_t portalEntityId, const PortalDefinition& portal);
    void SendPortalDespawn(const std::shared_ptr<PlayerSession>& receiver,
                           std::uint64_t portalEntityId, PortalDespawnReason reason);

    // 阶段21：请求入口（io 线程；Client 只表达意图，服务器全部重验）。
    void HandlePortalUseRequest(std::uint64_t connectionId, const legend::network::Packet& packet);
    void HandleRespawnRequest(std::uint64_t connectionId, const legend::network::Packet& packet);

    // 阶段21：响应/事件发送。
    void SendPortalUseResponse(const std::shared_ptr<PlayerSession>& player,
                               std::uint64_t requestId, bool success, PortalResultCode code,
                               std::uint16_t destinationMapId, float destinationX,
                               float destinationY, std::uint32_t goldCost, std::int64_t newGold);
    void SendRespawnResponse(const std::shared_ptr<PlayerSession>& player,
                             std::uint64_t requestId, bool success, RespawnResultCode code,
                             std::uint16_t mapId, float x, float y, std::uint32_t goldCost,
                             std::int64_t newGold);
    void SendPlayerRespawned(const std::shared_ptr<PlayerSession>& player);

    // 阶段21：复活执行编排（RespawnService 规则 -> 金币/状态/复活/切换/保护/事件）。
    bool ExecuteRespawn(const std::shared_ptr<PlayerSession>& player, RespawnMode mode,
                        std::uint64_t requestId);

    // NPC 生成与 AOI。
    void SpawnInitialNpcs();
    void UpdatePlayerNpcVisibility(const std::shared_ptr<PlayerSession>& player,
                                   bool initialVisibility);
    void SendNpcSpawn(const std::shared_ptr<PlayerSession>& receiver, const NpcEntity& npc);
    void SendNpcDespawn(const std::shared_ptr<PlayerSession>& receiver, std::uint64_t npcEntityId,
                        NpcDespawnReason reason);
    void SendNpcQuestMarkerUpdate(const std::shared_ptr<PlayerSession>& player,
                                  const NpcEntity& npc, NpcQuestMarker marker);
    void SendDialogueToPlayer(const std::shared_ptr<PlayerSession>& player,
                              const DialoguePayload& payload);
    void CloseNpcSessions(const std::shared_ptr<PlayerSession>& player);

    // 请求入口（io 线程；Client 只表达意图，服务器全部重验）。
    void HandleNpcInteractRequest(std::uint64_t connectionId, const legend::network::Packet& packet);
    void HandleDialogueOptionRequest(std::uint64_t connectionId,
                                     const legend::network::Packet& packet);
    void HandleShopOpenRequest(std::uint64_t connectionId, const legend::network::Packet& packet);
    void HandleShopBuyRequest(std::uint64_t connectionId, const legend::network::Packet& packet);
    void HandleShopSellRequest(std::uint64_t connectionId, const legend::network::Packet& packet);
    void HandleTeleportRequest(std::uint64_t connectionId, const legend::network::Packet& packet);

    // 响应发送。
    void SendNpcInteractResponse(const std::shared_ptr<PlayerSession>& player,
                                 std::uint64_t requestId, bool success, NpcResultCode code,
                                 std::uint64_t npcEntityId, std::uint64_t dialogueSessionId,
                                 std::uint32_t dialogueId);
    void SendShopOpenResponse(const std::shared_ptr<PlayerSession>& player, std::uint64_t requestId,
                              bool success, ShopResultCode code, std::uint64_t shopSessionId,
                              std::uint32_t shopId, std::uint64_t npcEntityId,
                              const ShopDefinition* shop);
    void SendShopBuyResponse(const std::shared_ptr<PlayerSession>& player, std::uint64_t requestId,
                             bool success, ShopResultCode code, std::uint32_t itemDefinitionId,
                             std::uint32_t quantity, std::uint32_t goldSpent);
    void SendShopSellResponse(const std::shared_ptr<PlayerSession>& player, std::uint64_t requestId,
                              bool success, ShopResultCode code, std::uint32_t itemDefinitionId,
                              std::uint32_t quantity, std::uint32_t goldReceived);
    void SendTeleportResponse(const std::shared_ptr<PlayerSession>& player,
                              std::uint64_t requestId, bool success, TeleportResultCode code,
                              std::uint16_t mapId, float x, float y, std::uint32_t goldCost,
                              std::int64_t newGold);

    // Quest Option 复用阶段19 核心（指令二十八：不写第二套 Quest 逻辑）。
    QuestResultCode AcceptQuestForPlayer(const std::shared_ptr<PlayerSession>& player,
                                         QuestId questId);
    QuestResultCode BeginQuestTurnIn(const std::shared_ptr<PlayerSession>& player, QuestId questId,
                                     std::uint64_t requestId, bool sendResponsePacket);
    // Teleport Option 核心（指令六十三~七十三：验证 + 服务器权威执行）。
    bool TeleportPlayerViaNpc(const std::shared_ptr<PlayerSession>& player, std::uint64_t requestId,
                              std::uint64_t dialogueSessionId, std::uint32_t teleportId);

private:
    struct PendingTicket {
        std::uint64_t clientConnectionId = 0;
        std::chrono::steady_clock::time_point createdAt{std::chrono::steady_clock::now()};
    };

    // 阶段25.5：EnterWorld 加载结果（legacy DB 任务与 RPC 链共用同一应用逻辑）。
    struct EnterWorldLoadResult {
        bool characterOk = false; // 角色行已加载且未删除（归属校验在 Apply 内）
        std::string error;
        legend::account::CharacterRow row;
        std::vector<legend::world::InventoryRepository::InventoryRow> itemRows;
        std::vector<legend::world::QuestRepository::QuestRow> questRows;
        std::vector<legend::world::QuestRepository::ObjectiveRow> questObjectiveRows;
    };
    // 阶段25.5：进世界加载应用（io 线程；替代原 HandleConsumeResponse 内联体）。
    void ApplyEnterWorldLoad(std::uint64_t connectionId, std::uint64_t requestId,
                             std::uint64_t accountId, std::uint64_t characterId,
                             const EnterWorldLoadResult& load);
    bool UsingRpcPersistence() const { return m_config.dbPort != 0; }
    // 阶段25.5：成长/金币/离线奖励落库（legacy DbWorker 与 DbServer RPC 双模式共用入口）。
    void PersistProgression(std::uint64_t characterId, std::uint32_t level, std::int64_t exp,
                            std::int64_t gold);
    void PersistGold(std::uint64_t characterId, std::int64_t gold);
    void PersistOfflineReward(std::uint64_t characterId, std::int64_t expDelta,
                              std::int64_t goldDelta);
    // 阶段25.5：任务持久化（legacy DbWorker 与 DbServer RPC 双模式共用入口）。
    void PersistQuestInsert(std::uint64_t characterId, QuestId questId, std::uint8_t state,
                            std::int64_t acceptedAt,
                            const std::vector<std::uint32_t>& objectiveIds);
    void PersistQuestTurnIn(const QuestRepository::TurnInTransaction& tx,
                            const std::function<void(bool ok, std::uint64_t itemInstanceId,
                                                     const std::string& error)>& onDone);
    void PersistQuestAbandon(std::uint64_t characterId, QuestId questId, std::int64_t nowUnix);
    void PersistQuestObjective(std::uint64_t characterId, QuestId questId,
                               std::uint32_t objectiveId, std::uint32_t progress);
    void PersistQuestState(std::uint64_t characterId, QuestId questId, std::uint8_t state,
                           std::int64_t timestamp, bool setTurnedInAt);
    void PersistOfflineKill(std::uint64_t characterId,
                            const std::vector<QuestRepository::KillCandidate>& candidates);

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

    // 阶段17：怪物重生（指令二十三~二十九：runtime only，重启不持久化）
    MonsterRespawnManager m_respawnManager;
    asio::steady_timer m_respawnTimer;     // 指令二十四：Respawn Tick 250ms
    asio::steady_timer m_progressionTimer; // 指令十五：30s ProgressionSnapshot 纠偏

    // 阶段18：物品/掉落/背包（runtime；背包/装备持久化，World Drop 不持久化）。
    ItemRegistry m_itemRegistry;
    std::unique_ptr<DropRoller> m_dropRoller; // 指令十一：可注入 RNG
    WorldItemDropManager m_itemDrops;         // 指令十二/十七：Drop 容器 + Spatial Grid
    std::uint64_t m_nextItemDropId = 1;       // 指令十三：dropEntityId 单调（≠ instanceId）
    asio::steady_timer m_itemDropTimer;       // 指令四十三：Drop cleanup tick 500ms
    // 阶段19：QuestRegistry 硬编码单例（QuestRegistry::Instance()）；
    // QuestSnapshot 周期纠偏 Timer（指令四十七：10s，Stop 时 cancel）。
    asio::steady_timer m_questSnapshotTimer;

    // 阶段20：NPC 系统（NpcRegistry 单例；NPC 静态不移动/不死亡；runtime only）。
    NpcManager m_npcs;
    NpcSpatialGrid m_npcGrid;
    std::uint64_t m_nextDialogueSessionId = 1; // 指令二十一：dialogueSessionId 单调
    std::uint64_t m_nextShopSessionId = 1;     // 指令四十：shopSessionId 单调

    // 阶段21：多地图 / Portal 系统（Map/Portal Registry 单例；Portal 静态不移动）。
    PortalManager m_portals;
    PortalSpatialGrid m_portalGrid;
    MapTransitionService m_mapTransition; // 指令二十三/二十四：统一地图切换

    // 阶段11 指令二十五：World 独立 DB Worker（网络线程禁止直接 SQLite IO）
    // 阶段25.5：dbPort!=0 时改用 DbServer RPC（m_persistence），m_database/m_dbWorker 不启用。
    legend::account::Database m_database;
    legend::account::DbWorker m_dbWorker;
    std::shared_ptr<legend::server::PersistenceClient> m_persistence;

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
