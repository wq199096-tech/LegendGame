#pragma once

#include "Client/WorldNetwork/ClientNpcModels.h"
#include "Client/WorldNetwork/ClientQuestModel.h"
#include "Client/WorldNetwork/ClientWorldMapModel.h"
#include "Client/WorldNetwork/RemoteItemModels.h"
#include "Client/WorldNetwork/RemoteMonsterManager.h"
#include "Client/WorldNetwork/RemoteNpcManager.h"
#include "Client/WorldNetwork/RemotePlayerManager.h"
#include "Client/WorldNetwork/RemotePortalManager.h"
#include "Client/WorldNetwork/RemoteStatusEffectContainer.h"
#include "Client/WorldNetwork/WorldNetworkClient.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace legend::client {

// 阶段11 指令四十五/五十：WorldClientController——世界连接编排。
// CharacterSelect 成功后（拿到 selectionTicket）自动连接 WorldServer；
// 只更新自身状态/缓存，禁止直接修改 PlayerCharacter/Combat。
// 阶段12：持有 RemotePlayerManager（指令三十三，主线程独占，指令四十七）。
// 阶段13：持有 RemoteMonsterManager（指令五十二/六十）。
class WorldClientController {
public:
    WorldClientController();

    // 每帧由上层喂入世界网络事件（主线程消费）。
    void HandleEvent(const WorldNetworkEvent& event);
    void OnDisconnected();
    // 阶段12 指令三十七：每帧远程玩家插值（主线程）。
    void UpdateRemotePlayers(float deltaTime);
    // 阶段13 指令五十三：每帧远程怪物插值（主线程）。
    void UpdateRemoteMonsters(float deltaTime);

    // 指令四十六：CharacterSelect 成功后调用（自动连接 + EnterWorld）。
    void EnterWorldWithTicket(const std::string& selectionTicket);
    // 阶段11 指令三：世界端点配置（默认 127.0.0.1:7200）。
    void SetWorldEndpoint(const std::string& host, std::uint16_t port) {
        m_client->SetWorldEndpoint(host, port);
    }
    // 指令三十四：WorldReady 后发送移动输入（方向，禁止绝对坐标）。
    void SendMoveInput(float directionX, float directionY, float deltaTime);
    // 阶段14 指令五十九/六十一：Debug 攻击——只发目标（服务器重新验证）。
    void SendAttack(std::uint64_t targetEntityId);
    // 阶段15 指令五十五/五十九：Debug 施法——只发 skillId + 目标类型/ID
    //（伤害/Mana/CD/完成时间全部服务器权威，指令一/二）。
    void SendSkillCast(std::uint32_t skillId, std::uint8_t targetType,
                       std::uint64_t targetEntityId);
    void Disconnect();

    WorldFlowState State() const { return m_state; }
    bool IsWorldReady() const { return m_state == WorldFlowState::WorldReady; }
    std::uint64_t WorldConnectionId() const { return m_client->ServerConnectionId(); }
    std::uint64_t CharacterId() const { return m_characterId; }
    std::uint16_t MapId() const { return m_mapId; }
    // 阶段14 指令六十五/七十一：本地玩家 HP（服务器权威事件驱动维护）。
    std::uint32_t LocalCurrentHp() const { return m_localCurrentHp; }
    std::uint32_t LocalMaxHp() const { return m_localMaxHp; }
    bool LocalAlive() const { return m_localAlive; }
    std::uint64_t LastAttackRequestId() const { return m_lastAttackRequestId; }
    std::uint64_t LastSkillRequestId() const { return m_lastSkillRequestId; } // 阶段15
    // 阶段15 指令五十六：本地 Mana / 施法状态（服务器权威事件驱动维护）。
    std::uint32_t LocalCurrentMana() const { return m_localCurrentMana; }
    std::uint32_t LocalMaxMana() const { return m_localMaxMana; }
    bool LocalCasting() const { return m_localCasting; }
    std::uint64_t ActiveCastId() const { return m_activeCastId; }
    std::uint32_t ActiveSkillId() const { return m_activeSkillId; }
    float CastProgress() const; // 0~1（仅展示；完成必须等服务器事件，指令五十八）
    // 阶段16 指令六十四：本地玩家状态容器（仅展示；Remove 等 StatusRemoved/Snapshot）。
    const RemoteStatusEffectContainer& LocalStatusEffects() const { return m_localStatusEffects; }
    // 阶段17 指令三十：本地成长数据（服务器权威事件驱动维护：RewardGranted/
    // LevelUp/ProgressionSnapshot）。
    std::uint32_t LocalLevel() const { return m_localLevel; }
    std::int64_t LocalExperience() const { return m_localExperience; }
    std::int64_t LocalExpToNext() const { return m_localExpToNext; }
    std::int64_t LocalGold() const { return m_localGold; }

    // ------------------------------------------------------------------
    // 阶段18：掉落/背包/装备镜像与 Debug 操作（E/I/6/7/8/9）。
    // ------------------------------------------------------------------
    const RemoteWorldItemManager& WorldItems() const { return m_worldItems; }
    const ClientInventoryModel& Inventory() const { return m_inventory; }
    const ClientEquipmentModel& Equipment() const { return m_equipment; }
    std::uint64_t LastPickupRequestId() const { return m_lastPickupRequestId; }
    std::uint64_t LastEquipRequestId() const { return m_lastEquipRequestId; }
    // Debug E：拾取指定掉落（requestId 自动单调）。
    void SendPickup(std::uint64_t dropEntityId);
    // Debug 6：装备背包中第一件 Weapon；7：第一件 Armor（返回是否发出请求）。
    bool SendEquipFirstOf(std::uint32_t definitionId);
    // Debug 8/9：卸下 Weapon/Armor 槽。
    void SendUnequip(std::uint8_t equipmentSlot);
    // 背包中查找第一件指定 definition 的槽位（无则返回 false）。
    bool FindFirstBagSlotOf(std::uint32_t definitionId, std::uint32_t& outSlotIndex) const;

    // ------------------------------------------------------------------
    // 阶段19：任务镜像与 Debug 操作（F8 面板 + Ctrl/Shift/Alt+1~5）。
    // Client 不是任务真相（指令四十九）：只发 questId（指令二），状态/进度
    // 全部来自服务器事件。
    // ------------------------------------------------------------------
    const ClientQuestModel& Quests() const { return m_quests; }
    std::uint64_t LastQuestRequestId() const { return m_lastQuestRequestId; }
    // Debug Ctrl+1~5：Accept / Shift+1~5：TurnIn / Alt+1~5：Abandon。
    void SendQuestAccept(std::uint32_t questId);
    void SendQuestTurnIn(std::uint32_t questId);
    void SendQuestAbandon(std::uint32_t questId);
    // 指令五十一：F8 Quest Debug 面板文本。
    bool QuestDebugVisible() const { return m_questDebugVisible; }
    void ToggleQuestDebug() { m_questDebugVisible = !m_questDebugVisible; }
    std::string QuestStatusText() const { return m_quests.DebugText(); }
    // 阶段20：F8 面板追加 NPC/对话/商店状态（名字列表 + Marker + 菜单 Options）。
    std::string NpcStatusText() const;

    // ------------------------------------------------------------------
    // 阶段20：NPC 镜像与 Debug 交互（E 交互 / 对话数字键 1~9 / 商店 B 买 S 卖）。
    // ------------------------------------------------------------------
    const RemoteNpcManager& Npcs() const { return m_npcs; }
    const ClientDialogueModel& Dialogue() const { return m_dialogue; }
    const ClientShopModel& Shop() const { return m_shop; }
    std::uint64_t LastNpcRequestId() const { return m_lastNpcRequestId; }

    // ------------------------------------------------------------------
    // 阶段21：地图/Portal/复活镜像与操作（F 传送门 / R·T 复活 / F9 面板）。
    // ------------------------------------------------------------------
    const RemotePortalManager& Portals() const { return m_portals; }
    const ClientWorldMapModel& MapModel() const { return m_map; }
    // 死亡展示数据（PlayerDeath 记录时刻；3 秒倒计时仅展示，服务器权威判断）。
    std::chrono::steady_clock::time_point LocalDeathTime() const { return m_localDeathTime; }
    // 指令一百一十三：复活保护展示（PlayerRespawned 后 3 秒内）。
    std::chrono::steady_clock::time_point LocalRespawnTime() const { return m_localRespawnTime; }
    // 指令十九：F —— 发 PortalUseRequest（选交互半径内最近 visible Portal）。
    bool SendPortalUseNearest(float selfX, float selfY);
    // 指令三十三：R/T —— 发 RespawnRequest（mode 1=CurrentMap 2=Town）。
    bool SendRespawnRequest(std::uint8_t respawnMode);
    // 指令一百一十二：F9 Map Debug Panel 文本。
    std::string MapStatusText() const;
    // 指令十七：E —— 找最近 visible NPC 且距离<=120（Client 选最近仅便利，
    // 服务器重新验证）；返回是否发出请求。
    bool SendInteractNearestNpc(float selfX, float selfY);
    // 指令八十：数字键 1~9 选择对话 Option（0-based 传 index+1）。
    bool SendDialogueOptionByIndex(std::size_t oneBased);
    // 指令八十一：B 买选中条目（quantity=1，Material 可配）；S 卖（按背包实例）。
    bool SendBuySelected(std::uint32_t quantity = 1);
    bool SendSellSelected(std::uint64_t inventoryInstanceId, std::uint32_t quantity);
    void SendShopOpenRequest();
    // 指令六十三：Teleport Option 选择。
    bool SendTeleportByOptionIndex(std::size_t oneBased);
    // 阶段12 指令三十三/七十二：远程玩家容器与 Debug 统计。
    const RemotePlayerManager& RemotePlayers() const { return m_remotePlayers; }
    std::uint32_t LastRemoteBatchSize() const { return m_lastRemoteBatchSize; }
    // 阶段13 指令五十二/五十八：远程怪物容器与 Debug 统计。
    const RemoteMonsterManager& RemoteMonsters() const { return m_remoteMonsters; }
    std::uint32_t LastMonsterBatchSize() const { return m_lastMonsterBatchSize; }
    float ServerPositionX() const { return m_serverPositionX; }
    float ServerPositionY() const { return m_serverPositionY; }
    std::uint32_t LastServerInputSequence() const { return m_lastServerSequence; }
    std::uint32_t LastSentInputSequence() const { return m_lastSentSequence; }
    float RttMs() const { return m_client->LastRttMs(); }
    std::uint16_t LastErrorCode() const { return m_lastErrorCode; }
    const std::string& LastError() const { return m_lastError; }
    const WorldNetworkClient& Client() const { return *m_client; }
    WorldNetworkClient& Client() { return *m_client; }

private:
    void SetState(WorldFlowState state);
    void HandleStatusEvent(const WorldNetworkEvent& event); // 阶段16：状态事件路由

    std::shared_ptr<WorldNetworkClient> m_client = std::make_shared<WorldNetworkClient>();
    WorldFlowState m_state = WorldFlowState::Disconnected;
    RemotePlayerManager m_remotePlayers; // 阶段12 指令三十三
    std::uint32_t m_lastRemoteBatchSize = 0;
    RemoteMonsterManager m_remoteMonsters; // 阶段13 指令五十二
    std::uint32_t m_lastMonsterBatchSize = 0;

    std::uint64_t m_characterId = 0;
    std::uint16_t m_mapId = 1;
    float m_serverPositionX = 0.0f;
    float m_serverPositionY = 0.0f;
    std::uint32_t m_lastServerSequence = 0;
    std::uint32_t m_lastSentSequence = 0;
    std::uint16_t m_lastErrorCode = 0;
    std::string m_lastError;
    // 阶段14：本地玩家 HP（服务器权威：EnterWorldSuccess 初始化，CombatEvent/
    // HealthSnapshot/PlayerDeath 更新）。
    std::uint32_t m_localCurrentHp = 100;
    std::uint32_t m_localMaxHp = 100;
    bool m_localAlive = true;
    std::uint64_t m_lastAttackRequestId = 0;
    // 阶段15 指令五十六：本地 Mana / 施法状态（服务器权威事件驱动）。
    std::uint32_t m_localCurrentMana = 100;
    std::uint32_t m_localMaxMana = 100;
    bool m_localCasting = false;
    std::uint64_t m_activeCastId = 0;
    std::uint32_t m_activeSkillId = 0;
    std::uint64_t m_castStartSteadyMs = 0; // 本地展示计时（steady，仅进度条）
    std::uint32_t m_castDurationMs = 0;
    std::uint64_t m_lastSkillRequestId = 0;
    // 阶段16 指令六十四：本地玩家状态容器。
    RemoteStatusEffectContainer m_localStatusEffects;
    // 阶段17 指令三十：本地成长数据（Level/Experience/ExpToNext/Gold）。
    std::uint32_t m_localLevel = 1;
    std::int64_t m_localExperience = 0;
    std::int64_t m_localExpToNext = 100;
    std::int64_t m_localGold = 0;
    // 阶段18：掉落/背包/装备镜像 + 请求 id 计数。
    RemoteWorldItemManager m_worldItems;
    ClientInventoryModel m_inventory;
    ClientEquipmentModel m_equipment;
    std::uint64_t m_nextItemRequestId = 1;
    std::uint64_t m_lastPickupRequestId = 0;
    std::uint64_t m_lastEquipRequestId = 0;
    // 阶段19：任务镜像 + 请求 id 计数 + F8 面板开关。
    ClientQuestModel m_quests;
    std::uint64_t m_nextQuestRequestId = 1;
    std::uint64_t m_lastQuestRequestId = 0;
    bool m_questDebugVisible = false;
    // 阶段20：NPC 镜像 + 对话/商店模型 + 请求 id 计数。
    RemoteNpcManager m_npcs;
    ClientDialogueModel m_dialogue;
    ClientShopModel m_shop;
    std::uint64_t m_nextNpcRequestId = 1;
    std::uint64_t m_lastNpcRequestId = 0;
    // 阶段21：Portal/地图镜像 + 地图类请求 id 计数 + 死亡/复活展示时间。
    RemotePortalManager m_portals;
    ClientWorldMapModel m_map;
    std::uint64_t m_nextMapRequestId = 1;
    std::uint64_t m_lastMapRequestId = 0;
    std::chrono::steady_clock::time_point m_localDeathTime{};
    std::chrono::steady_clock::time_point m_localRespawnTime{};
};

} // namespace legend::client
