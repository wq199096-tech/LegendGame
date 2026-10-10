#pragma once

#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpClient.h"

#include "Shared/Combat/CombatProtocol.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/Dialogue/DialogueProtocol.h"
#include "Shared/Item/ItemProtocol.h"
#include "Shared/Item/ItemTypes.h"
#include "Shared/Monster/MonsterProtocol.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/Network/MessageId.h"
#include "Shared/Network/NetworkConstants.h"
#include "Shared/Npc/NpcError.h"
#include "Shared/Npc/NpcProtocol.h"
#include "Shared/Npc/NpcTypes.h"
#include "Shared/Portal/PortalProtocol.h"
#include "Shared/Portal/PortalTypes.h"
#include "Shared/Quest/QuestProtocol.h"
#include "Shared/Quest/QuestTypes.h"
#include "Shared/Shop/ShopProtocol.h"
#include "Shared/Shop/ShopTypes.h"
#include "Shared/Skill/SkillProtocol.h"
#include "Shared/Skill/SkillTypes.h"
#include "Shared/Status/StatusEffectProtocol.h"
#include "Shared/Status/StatusEffectTypes.h"
#include "Shared/Teleport/TeleportProtocol.h"
#include "Shared/Teleport/TeleportTypes.h"
#include "Shared/World/WorldProtocol.h"
#include "Shared/WorldMap/MapProtocol.h"
#include "Shared/WorldMap/RespawnProtocol.h"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace legend::client {

// 阶段11 指令四十四：WorldFlowState（客户端世界流程状态机）。
enum class WorldFlowState {
    Disconnected,
    Connecting,
    Handshaking,
    WaitingEnterWorld,
    EnteringWorld,
    WorldReady,
    Failed,
};

const char* WorldFlowStateName(WorldFlowState state);

// 阶段16 指令五十五：状态事件数据（Applied/Updated/Removed/Snapshot 共用）。
struct StatusEventData {
    std::uint64_t instanceId = 0;
    std::uint32_t effectId = 0;
    std::uint8_t targetType = 0;      // CombatEntityType
    std::uint64_t targetEntityId = 0;
    std::uint8_t sourceType = 0;
    std::uint64_t sourceEntityId = 0;
    std::uint32_t sourceSkillId = 0;
    std::uint8_t stacks = 1;
    std::uint32_t durationMs = 0;
    std::uint32_t remainingMs = 0;
    std::uint8_t reason = 0;          // StatusRemovedReason
    std::uint64_t serverTime = 0;
    std::vector<world::StatusEffectSnapshotEntry> snapshotEffects; // Snapshot
};

// 阶段11 指令四十九：WorldNetworkEvent（world 网络线程 -> 主线程事件队列）。
struct WorldNetworkEvent {
    enum class Type {
        Connected,
        ConnectFailed,
        Disconnected,
        HandshakeSuccess,
        HandshakeFailed,
        EnterWorldSuccess,
        EnterWorldFailed,
        // 阶段26 指令十七：主动离开世界（服务器已保存并移除玩家）。
        LeaveWorldSuccess,
        LeaveWorldFailed,
        PositionSnapshot,
        ProtocolError,
        // 阶段12 指令四十六：AOI 多玩家同步事件
        PlayerSpawn,
        PlayerDespawn,
        RemotePlayerBatchSnapshot,
        // 阶段13 指令五十九：服务器权威怪物事件
        MonsterSpawn,
        MonsterDespawn,
        MonsterBatchSnapshot,
        // 阶段14 指令六十二：服务器权威战斗事件
        AttackResponse,
        CombatEvent,
        HealthSnapshot,
        MonsterDeath,
        PlayerDeath,
        // 阶段15 指令五十五：服务器权威技能事件
        SkillCastResponseEvent,
        SkillCastStartedEvent,
        SkillCastCompletedEvent,
        SkillCastCancelledEvent,
        SkillImpact,
        ManaSnapshot,
        // 阶段16 指令五十五：服务器权威状态事件
        StatusAppliedEvent,
        StatusUpdatedEvent,
        StatusRemovedEvent,
        StatusSnapshotEvent,
        // 阶段17 指令十三~十五：服务器权威成长/奖励事件
        RewardGrantedEvent,
        LevelUpEvent,
        ProgressionSnapshotEvent,
        // 阶段18：服务器权威掉落/背包/装备事件
        WorldItemSpawnEvent,
        WorldItemDespawnEvent,
        ItemPickupResponseEvent,
        InventorySnapshotEvent,
        InventoryDeltaEvent,
        EquipItemResponseEvent,
        UnequipItemResponseEvent,
        EquipmentSnapshotEvent,
        // 阶段19：服务器权威任务事件
        QuestAcceptResponseEvent,
        QuestTurnInResponseEvent,
        QuestAbandonResponseEvent,
        QuestProgressUpdatedEvent,
        QuestStateChangedEvent,
        QuestSnapshotEvent,
        QuestRewardGrantedEvent,
        // 阶段20：NPC / Dialogue / Shop / Teleport 事件
        NpcSpawnEvent,
        NpcDespawnEvent,
        NpcInteractResponseEvent,
        DialoguePayloadEvent,
        NpcQuestMarkerEvent,
        ShopOpenResponseEvent,
        ShopBuyResponseEvent,
        ShopSellResponseEvent,
        TeleportResponseEvent,
        // 阶段21：地图 / Portal / 复活事件
        PortalSpawnEvent,
        PortalDespawnEvent,
        PortalUseResponseEvent,
        MapChangedEvent,
        MapSnapshotEvent,
        RespawnResponseEvent,
        PlayerRespawnedEvent,
        // Stage27 指令十三：聊天事件
        ChatSendResponseEvent,
        ChatMessageEvent,
    };
    Type type = Type::Disconnected;
    std::string message;

    // 阶段17 指令十三~十五：成长/奖励事件数据（三个事件共用）。
    struct ProgressionEventData {
        std::uint64_t sourceMonsterEntityId = 0;
        std::uint32_t expGranted = 0;
        std::uint32_t goldGranted = 0;
        std::int64_t newExperience = 0;
        std::int64_t newGold = 0;
        std::uint32_t level = 0;
        std::uint32_t oldLevel = 0;
        std::uint32_t newLevel = 0;
        std::int64_t currentExp = 0;
        std::int64_t expToNext = 0;
        std::uint32_t newMaxHp = 0;
        std::uint32_t newAttackPower = 0;
        std::uint32_t newDefense = 0;
        std::uint64_t serverTime = 0;
    };
    ProgressionEventData progression;

    // EnterWorldResponse / PlayerSpawn 字段（指令二十/二十二）
    std::uint64_t requestId = 0;
    std::uint64_t accountId = 0;
    std::uint64_t characterId = 0;
    std::string characterName;
    std::uint16_t classId = 0;
    std::uint16_t gender = 0;
    std::uint32_t level = 1;
    std::uint16_t mapId = 1;
    float positionX = 0.0f;
    float positionY = 0.0f;
    std::uint16_t errorCode = 0;
    // Stage27 指令四：服务器权威朝向（Direction8；PlayerSpawn/批量快照携带）。
    std::uint8_t direction = 0;

    // PlayerPositionSnapshot 字段（指令四十二）
    std::uint32_t lastProcessedInputSequence = 0;

    // 阶段12：PlayerDespawn reason（指令二十三）+ batch 快照（指令二十五）
    std::uint8_t despawnReason = 0;
    std::uint64_t serverTime = 0;
    std::vector<world::RemotePlayerBatchEntry> batchPlayers;

    // 阶段13：Monster 事件字段（指令五十九；Spawn 元数据复用 name/level/mapId/x/y）
    std::uint64_t monsterEntityId = 0;
    std::uint32_t monsterTypeId = 0;
    std::uint8_t monsterState = 0;
    std::vector<world::MonsterSnapshotEntry> monsterBatch;

    // 阶段14：Combat 事件字段（指令六十二；EnterWorldResponse/PlayerSpawn/
    // MonsterSpawn 复用 currentHp/maxHp/alive）
    bool success = false;           // AttackResponse
    std::uint8_t resultCode = 0;    // AttackResponse（CombatResultCode）
    std::uint64_t targetEntityId = 0; // AttackResponse
    std::uint64_t eventId = 0;      // CombatEvent
    std::uint8_t attackerType = 0;  // CombatEvent / PlayerDeath(killerType)
    std::uint64_t attackerId = 0;   // CombatEvent / PlayerDeath(killerId)
    std::uint8_t targetType = 0;    // CombatEvent
    std::uint64_t targetId = 0;     // CombatEvent
    std::uint32_t damage = 0;       // CombatEvent
    std::uint32_t targetHpAfter = 0; // CombatEvent
    std::uint32_t targetMaxHp = 0;  // CombatEvent
    bool killed = false;            // CombatEvent
    std::uint8_t entityType = 0;    // HealthSnapshot（CombatEntityType）
    std::uint64_t entityId = 0;     // HealthSnapshot
    std::uint32_t currentHp = 0;    // HealthSnapshot
    std::uint32_t maxHp = 0;        // HealthSnapshot
    bool alive = true;              // HealthSnapshot

    // 阶段15：技能事件字段（指令五十五；EnterWorldResponse 复用 currentMana/maxMana）
    std::uint8_t sourceType = 0;    // CombatEvent 伤害来源（CombatSource，指令三十二）
    std::uint64_t sourceId = 0;     // CombatEvent 来源 ID（普通攻击 0 / 技能 skillId）
    std::uint32_t skillId = 0;      // SkillCastResponse/Started/Completed/Cancelled/Impact
    std::uint64_t castId = 0;       // Started/Completed/Cancelled/Impact
    std::uint8_t skillTargetType = 0; // Started/Completed（SkillTargetType）
    std::uint32_t castTimeMs = 0;   // Started
    std::uint8_t cancelReason = 0;  // Cancelled（SkillCancelReason）
    std::uint8_t skillResultCode = 0; // SkillCastResponse（SkillResultCode）
    bool accepted = false;          // SkillCastResponse
    std::uint32_t currentMana = 0;  // SkillCastResponse / ManaSnapshot
    std::uint32_t maxManaVal = 0;   // ManaSnapshot（maxMana 名与 CombatEvent 冲突改用）
    std::vector<world::SkillImpactTarget> impactTargets; // Impact
    StatusEventData status;         // 阶段16：状态事件（Applied/Updated/Removed/Snapshot）
    // 阶段18：物品/背包/装备事件数据（复用 entityId/dropEntityId/resultCode 等）。
    std::uint64_t dropEntityId = 0;   // WorldItemSpawn/Despawn、ItemPickupResponse
    std::uint32_t itemDefinitionId = 0; // WorldItemSpawn
    std::uint32_t itemQuantity = 1;     // WorldItemSpawn / Inventory 条目
    bool itemOwnedByYou = false;        // WorldItemSpawn isOwnedByYou
    std::uint32_t ownerLockRemainingMs = 0; // WorldItemSpawn
    std::uint8_t itemDespawnReason = 0;     // WorldItemDespawn
    std::uint8_t itemResultCode = 0;        // ItemPickupResponse/Equip/Unequip（ItemResultCode）
    std::uint8_t inventoryOpcode = 0;       // InventoryDelta（1=Set 2=Remove）
    std::uint8_t itemEquipmentSlot = 0;     // Equip/Unequip Response（EquipmentSlot）
    std::uint32_t inventorySlotIndex = 0;   // Inventory 条目槽位
    std::uint64_t inventoryInstanceId = 0;  // Inventory 条目 instanceId
    std::vector<world::InventoryEntryData> inventoryEntries; // InventorySnapshot
    world::EquipmentSnapshotPayload equipmentSnapshot;       // EquipmentSnapshot
    // 阶段19：任务事件数据（复用 resultCode/requestId/serverTime 等字段）。
    std::uint32_t questId = 0;                       // 全部 Quest 事件
    std::uint32_t questObjectiveId = 0;              // QuestProgressUpdated
    std::uint32_t questObjectiveCurrent = 0;         // QuestProgressUpdated
    std::uint32_t questObjectiveRequired = 0;        // QuestProgressUpdated
    std::uint8_t questState = 0;                     // QuestProgressUpdated/StateChanged
    std::uint8_t questOldState = 0;                  // QuestStateChanged
    std::uint8_t questResultCode = 0;                // Accept/TurnIn/Abandon Response
    std::uint32_t questRewardExp = 0;                // QuestRewardGranted
    std::uint32_t questRewardGold = 0;               // QuestRewardGranted
    std::uint32_t questRewardItemDefinitionId = 0;   // QuestRewardGranted
    std::uint32_t questRewardItemQuantity = 0;       // QuestRewardGranted
    std::vector<world::QuestSnapshotEntryData> questSnapshot; // QuestSnapshot
    // 阶段20：NPC/Dialogue/Shop/Teleport 事件数据（复用 resultCode/requestId 等）。
    std::uint64_t npcEntityId = 0;                   // NpcSpawn/Despawn/Interact/Marker/Shop
    std::uint32_t npcDefinitionId = 0;               // NpcSpawn/Marker
    std::string npcName;                             // NpcSpawn
    std::uint8_t npcType = 0;                        // NpcSpawn（NpcType）
    std::uint32_t visualId = 0;                      // NpcSpawn
    std::uint8_t npcDespawnReason = 0;               // NpcDespawn
    std::uint64_t dialogueSessionId = 0;             // Interact/DialoguePayload/Teleport
    std::uint32_t dialogueId = 0;                    // Interact
    world::NpcQuestMarker questMarker = world::NpcQuestMarker::None; // Marker
    world::DialoguePayload dialoguePayload;          // DialoguePayload
    std::uint64_t shopSessionId = 0;                 // ShopOpen
    world::ShopOpenResponsePayload shopOpen;         // ShopOpen
    world::ShopBuyResponsePayload shopBuy;           // ShopBuy
    world::ShopSellResponsePayload shopSell;         // ShopSell
    world::TeleportResponsePayload teleport;         // Teleport
    // 阶段21：地图/Portal/复活事件数据（复用 mapId/positionX/Y/requestId 等）。
    std::uint64_t portalEntityId = 0;                // PortalSpawn/Despawn/Use
    std::uint32_t portalId = 0;                      // PortalSpawn
    std::string portalName;                          // PortalSpawn
    std::string portalDestinationName;               // PortalSpawn（目标地图名，F9 面板用）
    float portalInteractionRadius = 0.0f;            // PortalSpawn
    std::uint16_t portalDestinationMapId = 1;        // PortalSpawn
    std::uint8_t portalDespawnReason = 0;            // PortalDespawn
    world::PortalUseResponsePayload portalUse;       // PortalUseResponse
    world::MapChangedPayload mapChanged;             // MapChanged
    world::MapSnapshotPayload mapSnapshot;           // MapSnapshot
    world::RespawnResponsePayload respawn;           // RespawnResponse
    world::PlayerRespawnedPayload playerRespawned;   // PlayerRespawned
    // Stage27：聊天事件数据（ChatSendResponse/ChatMessageEvent）。
    struct ChatEventData {
        std::uint64_t requestId = 0;        // ChatSendResponse
        bool success = false;               // ChatSendResponse
        std::uint16_t errorCode = 0;        // ChatErrorCode
        std::string errorMessage;           // 服务器中文文案
        std::uint64_t messageId = 0;        // ChatMessageEvent（服务器单调）
        std::uint8_t channel = 0;           // ChatChannel
        std::uint64_t senderCharacterId = 0;// System 消息为 0
        std::string senderName;
        std::string targetName;
        std::string text;
        std::uint64_t timestamp = 0;
    };
    ChatEventData chat;
};

// 阶段11 指令四十五/四十七/四十八/七十七/七十八：
// WorldNetworkClient —— 独立的世界频道连接（正式环境经 Gateway 统一接入）。
// - 异步连接，失败不阻塞游戏（状态 Failed）
// - network thread -> event queue -> main thread（禁止跨线程改游戏对象）
// - 独立 Heartbeat 状态（5s Ping / 15s timeout），不复用 GameNetworkClient
class WorldNetworkClient : public std::enable_shared_from_this<WorldNetworkClient> {
public:
    struct Config {
        std::string worldHost = "127.0.0.1";
        std::uint16_t worldPort = 7300;
        std::string clientBuild = "0.11.0";
        std::string clientName = "LegendClient";
        double heartbeatIntervalSeconds = 5.0;
        double heartbeatTimeoutSeconds = 15.0;
    };

    WorldNetworkClient();
    ~WorldNetworkClient();

    void Connect(); // 异步（端点经 SetWorldEndpoint 配置）
    void Disconnect(bool notifyServer); // 指令七十九：发 WorldDisconnectNotice

    // Stage25.5：正式默认走 Gateway 7300；测试仍可显式指定 World 端口。
    void SetWorldEndpoint(const std::string& host, std::uint16_t port) {
        m_config.worldHost = host;
        m_config.worldPort = port;
    }

    void SendEnterWorld(const std::string& selectionTicket);
    // 阶段26 指令十七：主动离开世界（仅 WorldReady；收到 LeaveWorldResponse 后
    // 本地断开 world 连接，Gateway 回退会话状态）。
    void SendLeaveWorld();
    void SendMoveInput(std::uint32_t inputSequence, float directionX, float directionY,
                       float deltaTime);
    // 阶段14 指令五十九/六十一：Debug 攻击——只发"我想攻击谁"
    //（禁止传坐标/hitbox/damage，指令三；服务器重新验证，指令六十）。
    void SendAttack(std::uint64_t requestId, std::uint8_t targetEntityType,
                    std::uint64_t targetEntityId);
    // 阶段15 指令二十二/二十三：Debug 施法——只发 skillId + 目标类型/ID
    //（禁止传伤害/Mana/CD/CastTime/AOE 位置/命中结果，指令二十三/九十六/九十七）。
    void SendSkillCast(std::uint64_t requestId, std::uint32_t skillId, std::uint8_t targetType,
                       std::uint64_t targetEntityId);

    // 阶段18 指令二十一：拾取——只发 requestId + dropEntityId（禁止上报
    // itemDefinitionId/quantity/position，指令一/二十一）。
    void SendItemPickup(std::uint64_t requestId, std::uint64_t dropEntityId);
    // 阶段18 指令二十七：装备背包槽位物品（服务器按权威槽内容校验）。
    void SendEquipItem(std::uint64_t requestId, std::uint32_t slotIndex);
    // 阶段18 指令三十四：卸下装备槽。
    void SendUnequipItem(std::uint64_t requestId, std::uint8_t equipmentSlot);

    // 阶段19 指令二：Quest 请求——Client 只发 Accept/TurnIn/Abandon 三个
    // requestId + questId（绝不能上报进度/状态/奖励，指令二）。
    void SendQuestAccept(std::uint64_t requestId, std::uint32_t questId);
    void SendQuestTurnIn(std::uint64_t requestId, std::uint32_t questId);
    void SendQuestAbandon(std::uint64_t requestId, std::uint32_t questId);

    // 阶段20 指令二：NPC 链路——Client 只表达意图（requestId + id），全部服务器重验。
    void SendNpcInteract(std::uint64_t requestId, std::uint64_t npcEntityId);
    void SendDialogueOption(std::uint64_t requestId, std::uint64_t dialogueSessionId,
                            std::uint32_t optionId);
    void SendShopOpen(std::uint64_t requestId, std::uint64_t dialogueSessionId);
    void SendShopBuy(std::uint64_t requestId, std::uint64_t shopSessionId,
                     std::uint32_t itemDefinitionId, std::uint32_t quantity);
    void SendShopSell(std::uint64_t requestId, std::uint64_t shopSessionId,
                      std::uint64_t inventoryInstanceId, std::uint32_t quantity);
    void SendTeleport(std::uint64_t requestId, std::uint64_t dialogueSessionId,
                      std::uint32_t teleportId);

    // 阶段21 指令二十/三十三：Client 只表达意图——PortalUse（requestId +
    // portalEntityId，禁止上传目标 map/坐标/费用）与 Respawn（requestId + mode）。
    void SendPortalUse(std::uint64_t requestId, std::uint64_t portalEntityId);
    void SendRespawn(std::uint64_t requestId, std::uint8_t respawnMode);

    // Stage27 指令十三/十七：聊天请求——客户端只能发 requestId/channel/targetName/text；
    // sender 身份由服务器根据 WorldSession 确定（禁止伪造 senderName/System）。
    void SendChat(std::uint64_t requestId, std::uint8_t channel,
                  const std::string& targetName, const std::string& text);

    void PollEvents(std::deque<WorldNetworkEvent>& out); // 主线程消费
    void UpdateHeartbeat(float deltaTime);

    WorldFlowState State() const { return m_state.load(); }
    std::uint64_t ServerConnectionId() const { return m_serverConnectionId.load(); }
    float LastRttMs() const { return m_lastRttMs.load(); }
    std::string LastError() const;
    const Config& GetConfig() const { return m_config; }

private:
    void OnTransportConnected(std::shared_ptr<legend::net::TcpConnection> connection);
    void OnPacket(const legend::network::Packet& packet);
    void SendWorldHello();
    void SendHeartbeat();
    void ScheduleHeartbeat();
    void PushEvent(WorldNetworkEvent event);
    void SetState(WorldFlowState state, const std::string& error = {});

    legend::net::NetworkService m_service;
    std::shared_ptr<legend::net::TcpClient> m_client;
    std::shared_ptr<legend::net::TcpConnection> m_connection;
    Config m_config;

    std::atomic<WorldFlowState> m_state{WorldFlowState::Disconnected};
    std::atomic<std::uint64_t> m_serverConnectionId{0};
    std::atomic<float> m_lastRttMs{-1.0f};

    std::mutex m_eventMutex;
    std::deque<WorldNetworkEvent> m_events;
    mutable std::mutex m_errorMutex;
    std::string m_lastError;

    asio::steady_timer m_heartbeatTimer;
    std::chrono::steady_clock::time_point m_lastPongTime{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point m_lastPingTime{};
    std::uint32_t m_pingSequence = 0;
    std::atomic<bool> m_heartbeatScheduled{false};
    std::string m_pendingTicket; // 握手成功后自动发送 EnterWorld（指令四十六）
};

} // namespace legend::client
