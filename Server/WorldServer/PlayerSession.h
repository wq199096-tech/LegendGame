#pragma once

#include "Server/WorldServer/Item/InventoryContainer.h"
#include "Server/WorldServer/Npc/NpcEntity.h"
#include "Server/WorldServer/Quest/PlayerQuestContainer.h"
#include "Server/WorldServer/Status/StatusEffectContainer.h"

#include "Shared/Combat/CombatTypes.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/Progression/ProgressionTypes.h"
#include "Shared/Quest/QuestTypes.h"
#include "Shared/Skill/SkillDefinition.h"
#include "Shared/Skill/SkillTypes.h"
#include "Shared/World/WorldTypes.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace legend::world {

// 阶段15 指令十四：PendingSkillCast —— Cast-Time 技能的进行中施法状态
//（封装在 PlayerSession；WorldServer Skill Tick 统一检查 castCompleteTime）。
class PendingSkillCast {
public:
    bool active = false; // isCasting
    std::uint64_t castId = 0;            // 指令二十六：WorldServer 单调 castId
    SkillId skillId = 0;
    std::uint64_t castRequestId = 0;     // 触发本次施法的 Client requestId
    std::uint8_t targetType = 0;         // SkillTargetType
    std::uint64_t targetEntityId = 0;
    std::chrono::steady_clock::time_point castStartTime{};
    std::chrono::steady_clock::time_point castCompleteTime{};
};

// 阶段11 指令二十七：PlayerSession —— 进入世界后的权威玩家数据。
// 由 WorldManager 持有（io 线程访问）；位置为服务器权威（指令三十三）。
// 阶段12 指令十三/十四：visiblePlayers 由 WorldServer 权威维护（Client 不决定谁可见）。
class PlayerSession {
public:
    PlayerSession() = default;
    PlayerSession(std::uint64_t connectionId, std::uint64_t accountId, std::uint64_t characterId,
                  const std::string& characterName, std::uint16_t classId, std::uint16_t gender,
                  std::uint32_t level, std::uint16_t mapId, float positionX, float positionY);

    std::uint64_t ConnectionId() const { return m_connectionId; }
    std::uint64_t AccountId() const { return m_accountId; }
    std::uint64_t CharacterId() const { return m_characterId; }
    const std::string& CharacterName() const { return m_characterName; }
    std::uint16_t ClassId() const { return m_classId; }
    std::uint16_t Gender() const { return m_gender; }
    std::uint32_t Level() const { return m_level; }
    std::uint16_t MapId() const { return m_mapId; }
    // 阶段20 指令六十五：NPC 传送服务器权威更新 mapId（Client 不能决定）。
    void SetMapId(std::uint16_t mapId) { m_mapId = mapId; }

    float PositionX() const { return m_positionX; }
    float PositionY() const { return m_positionY; }
    void SetPosition(float x, float y) {
        m_positionX = x;
        m_positionY = y;
    }

    std::uint32_t LastProcessedInputSequence() const { return m_lastProcessedInputSequence; }
    void SetLastProcessedInputSequence(std::uint32_t sequence) {
        m_lastProcessedInputSequence = sequence;
    }

    bool IsPositionDirty() const { return m_dirtyPosition; }
    void SetPositionDirty(bool dirty) { m_dirtyPosition = dirty; }

    std::chrono::steady_clock::time_point LastMoveTime() const { return m_lastMoveTime; }
    void TouchMoveTime() { m_lastMoveTime = std::chrono::steady_clock::now(); }

    // 阶段12 指令十三：AOI 可见集合（characterId）。返回值 = 是否原本存在。
    const std::unordered_set<std::uint64_t>& VisiblePlayers() const { return m_visiblePlayers; }
    void AddVisiblePlayer(std::uint64_t characterId) { m_visiblePlayers.insert(characterId); }
    bool EraseVisiblePlayer(std::uint64_t characterId) {
        return m_visiblePlayers.erase(characterId) != 0;
    }
    void ClearVisiblePlayers() { m_visiblePlayers.clear(); }
    std::size_t VisibleCount() const { return m_visiblePlayers.size(); }

    // 阶段13 指令十三/十四：可见怪物集合（monsterEntityId），WorldServer 权威维护。
    const std::unordered_set<std::uint64_t>& VisibleMonsters() const { return m_visibleMonsters; }
    void AddVisibleMonster(std::uint64_t monsterEntityId) {
        m_visibleMonsters.insert(monsterEntityId);
    }
    bool EraseVisibleMonster(std::uint64_t monsterEntityId) {
        return m_visibleMonsters.erase(monsterEntityId) != 0;
    }
    void ClearVisibleMonsters() { m_visibleMonsters.clear(); }
    std::size_t VisibleMonsterCount() const { return m_visibleMonsters.size(); }

    // ------------------------------------------------------------------
    // 阶段14 指令四：基础战斗属性（阶段17 指令十：随等级成长，
    // 构造时按 level 初始化；升级经 ApplyLevelGrowth 更新）。
    // ------------------------------------------------------------------
    std::uint32_t MaxHp() const { return m_maxHp; }
    std::uint32_t CurrentHp() const { return m_currentHp; }
    std::uint32_t AttackPower() const { return m_attackPower; }
    std::uint32_t Defense() const { return m_defense; }
    float AttackRange() const { return m_attackRange; }
    float AttackCooldownSeconds() const { return m_attackCooldownSeconds; }
    bool Alive() const { return m_alive; }

    // ------------------------------------------------------------------
    // 阶段17 指令二：Player Progression（服务器权威；WorldServer 加载角色时
    // 从数据库读取；Client 不能发送"我要多少经验/金币"）。
    // ------------------------------------------------------------------
    std::int64_t Experience() const { return m_experience; }
    std::int64_t Gold() const { return m_gold; }
    void SetProgression(std::int64_t experience, std::int64_t gold) {
        m_experience = experience;
        m_gold = gold;
    }
    // 指令五/十：等级更新 + 基础属性成长 + CurrentHp 恢复到新 MaxHp（方便测试）。
    // Mana 不升级保持 100（指令十）。Derived Stats 由 WorldServer 统一重算。
    void ApplyLevelGrowth(std::uint32_t newLevel) {
        m_level = newLevel;
        m_maxHp = BaseMaxHpForLevel(newLevel);
        m_currentHp = m_maxHp; // 指令十二：升级直接回满
        m_attackPower = BaseAttackPowerForLevel(newLevel);
        m_defense = BaseDefenseForLevel(newLevel);
        m_maxMana = kPlayerMaxMana; // 指令十：BaseMaxMana 不升级
    }

    // 指令二十四：扣血（不低于 0；返回是否致死）。
    bool ApplyDamage(std::uint32_t damage) {
        m_currentHp = (m_currentHp > damage) ? (m_currentHp - damage) : 0u;
        if (m_currentHp == 0 && m_alive) {
            m_alive = false;
            return true;
        }
        return false;
    }
    // 指令七十九：死亡时刻（MarkDead/Revive 见阶段21 指令三十一/三十九）。
    std::chrono::steady_clock::time_point DeadSince() const { return m_deadSince; }
    // 测试白盒：抬高 HP 上限并回满（NPC 交互套件防死亡级联；生产不可调用）。
    void TestBuffHp(std::uint32_t hp) {
        m_maxHp = hp;
        m_currentHp = hp;
    }

    // 指令二十七/七：玩家攻击冷却（steady_clock，服务器权威）。
    std::chrono::steady_clock::time_point LastAttackTime() const { return m_lastAttackTime; }
    void TouchAttackTime() { m_lastAttackTime = std::chrono::steady_clock::now(); }
    // 指令七：当前攻击目标（monsterEntityId，调试/日志用）。
    std::uint64_t CombatTargetEntityId() const { return m_combatTargetEntityId; }
    void SetCombatTargetEntityId(std::uint64_t entityId) { m_combatTargetEntityId = entityId; }

    // 指令三十：最近 64 个攻击 requestId（防网络重放重复扣血）。
    // 只记录成功造成伤害的请求；重复 -> DuplicateRequest。
    bool IsRecentAttackRequest(std::uint64_t requestId) const {
        for (const auto id : m_recentAttackRequestIds) {
            if (id == requestId) {
                return true;
            }
        }
        return false;
    }
    void RememberAttackRequest(std::uint64_t requestId) {
        m_recentAttackRequestIds[m_recentAttackRequestCursor] = requestId;
        m_recentAttackRequestCursor =
            (m_recentAttackRequestCursor + 1) % m_recentAttackRequestIds.size();
    }

    // ------------------------------------------------------------------
    // 阶段15：Mana（指令十一/十二/六十八）。
    // 只有 WorldServer 扣 Mana（Client 不能发送剩余 Mana）；阶段15 不持久化
    //（WorldServer 重启/重新进入恢复 100/100）；无 Regen；永不为负。
    // ------------------------------------------------------------------
    std::uint32_t MaxMana() const { return m_maxMana; }
    std::uint32_t CurrentMana() const { return m_currentMana; }
    // 指令十九：施法被服务器正式接受时扣 Mana（不足返回 false；下限 0 保护）。
    bool ConsumeMana(std::uint32_t amount) {
        if (m_currentMana < amount) {
            return false;
        }
        m_currentMana -= amount;
        return true;
    }

    // ------------------------------------------------------------------
    // 阶段15 指令十三：每技能独立 Cooldown（nextReadyTime，steady_clock 权威）。
    // ------------------------------------------------------------------
    bool IsSkillReady(SkillId skillId, std::chrono::steady_clock::time_point now) const {
        auto it = m_skillNextReadyTime.find(skillId);
        return it == m_skillNextReadyTime.end() || now >= it->second;
    }
    // 指令二十：服务器正式接受施法时启动（不等命中；取消不返还）。
    void StartSkillCooldown(SkillId skillId, float cooldownSeconds) {
        m_skillNextReadyTime[skillId] =
            std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<float>(cooldownSeconds));
    }

    // ------------------------------------------------------------------
    // 阶段15 指令十四：CastingState —— 同一时间只能施放一个技能（指令十五）。
    // ------------------------------------------------------------------
    bool IsCasting() const { return m_casting.active; }
    const PendingSkillCast& Casting() const { return m_casting; }
    PendingSkillCast& CastingRef() { return m_casting; }
    void SetCasting(const PendingSkillCast& cast) { m_casting = cast; }
    void ClearCasting() { m_casting = PendingSkillCast{}; }

    // 指令七十四：最近 64 个已接受技能 requestId（防重放：不能重复扣 Mana/
    // 重复启动 CD/重复伤害）。失败请求允许重试（指令七十五，只缓存 accepted）。
    bool IsRecentSkillRequest(std::uint64_t requestId) const {
        for (const auto id : m_recentSkillRequestIds) {
            if (id == requestId) {
                return true;
            }
        }
        return false;
    }
    void RememberSkillRequest(std::uint64_t requestId) {
        m_recentSkillRequestIds[m_recentSkillRequestCursor] = requestId;
        m_recentSkillRequestCursor =
            (m_recentSkillRequestCursor + 1) % m_recentSkillRequestIds.size();
    }

    // ------------------------------------------------------------------
    // 阶段16 指令二十三/二十五：Base / Derived 战斗属性。
    // base 固定（20/5/120），effective 由 StatusEffectService::Recalculate-
    // DerivedStats 在状态 Applied/stack 变化/Removed/Expired 时更新（不每帧重算）。
    // ------------------------------------------------------------------
    std::uint32_t BaseAttackPower() const { return m_attackPower; }
    std::uint32_t BaseDefense() const { return m_defense; }
    float BaseMoveSpeed() const { return kWorldMoveSpeed; }
    std::uint32_t EffectiveAttackPower() const { return m_effectiveAttackPower; }
    std::uint32_t EffectiveDefense() const { return m_effectiveDefense; }
    float EffectiveMoveSpeed() const { return m_effectiveMoveSpeed; }
    void SetEffectiveCombatStats(std::uint32_t attackPower, std::uint32_t defense,
                                 float moveSpeed) {
        m_effectiveAttackPower = attackPower;
        m_effectiveDefense = defense;
        m_effectiveMoveSpeed = moveSpeed;
    }

    // 阶段16 指令十四：状态容器（runtime-only，死亡/断线/重启即消失）。
    StatusEffectContainer& StatusEffects() { return m_statusEffects; }
    const StatusEffectContainer& StatusEffects() const { return m_statusEffects; }

    // ------------------------------------------------------------------
    // 阶段18 指令十八：可见掉落集合（dropEntityId，WorldServer 权威维护）。
    // ------------------------------------------------------------------
    const std::unordered_set<std::uint64_t>& VisibleItemDrops() const {
        return m_visibleItemDrops;
    }
    void AddVisibleItemDrop(std::uint64_t dropEntityId) { m_visibleItemDrops.insert(dropEntityId); }
    bool EraseVisibleItemDrop(std::uint64_t dropEntityId) {
        return m_visibleItemDrops.erase(dropEntityId) != 0;
    }
    void ClearVisibleItemDrops() { m_visibleItemDrops.clear(); }

    // ------------------------------------------------------------------
    // 阶段18 指令六/七：服务器权威背包 + 装备槽（io 线程；持久化经 DbWorker）。
    // ------------------------------------------------------------------
    InventoryContainer& Inventory() { return m_inventory; }
    const InventoryContainer& Inventory() const { return m_inventory; }
    EquipmentSlots& EquipmentRef() { return m_equipment; }
    const EquipmentSlots& EquipmentRef() const { return m_equipment; }
    // 指令三十一：装备加成（Derived 重算时并入 Base）。
    std::uint32_t EquipmentAttackBonus() const {
        return m_equipment.weapon.quantity > 0 ? m_equippedWeaponAttack : 0;
    }
    std::uint32_t EquipmentDefenseBonus() const {
        return m_equipment.armor.quantity > 0 ? m_equippedArmorDefense : 0;
    }
    // WorldServer 装备变化后刷新加成缓存（由 ItemRegistry 查询结果写入）。
    void SetEquipmentBonuses(std::uint32_t attack, std::uint32_t defense) {
        m_equippedWeaponAttack = attack;
        m_equippedArmorDefense = defense;
    }

    // ------------------------------------------------------------------
    // 阶段18 指令三十六/三十七：最近 64 个成功 Pickup/Equip/Unequip requestId
    //（防重放：重复请求不重复获得物品/不重复变更装备）。
    // ------------------------------------------------------------------
    bool IsRecentItemRequest(std::uint64_t requestId) const {
        for (const auto id : m_recentItemRequestIds) {
            if (id == requestId) {
                return true;
            }
        }
        return false;
    }
    void RememberItemRequest(std::uint64_t requestId) {
        m_recentItemRequestIds[m_recentItemRequestCursor] = requestId;
        m_recentItemRequestCursor =
            (m_recentItemRequestCursor + 1) % m_recentItemRequestIds.size();
    }

    // ------------------------------------------------------------------
    // 阶段19 指令十四：PlayerQuestContainer —— 任务状态集中在 PlayerSession
    //（不散落 WorldServer 多个 map）。仅 io 线程访问。
    // ------------------------------------------------------------------
    PlayerQuestContainer& Quests() { return m_quests; }
    const PlayerQuestContainer& Quests() const { return m_quests; }

    // 阶段19 指令五十九：最近 64 个成功 Quest requestId（Accept/TurnIn/Abandon
    // 统一 QuestRequestHistory 防重放；TurnIn 防重放最高优先级——指令六十）。
    bool IsRecentQuestRequest(std::uint64_t requestId) const {
        for (const auto id : m_recentQuestRequestIds) {
            if (id == requestId) {
                return true;
            }
        }
        return false;
    }
    void RememberQuestRequest(std::uint64_t requestId) {
        m_recentQuestRequestIds[m_recentQuestRequestCursor] = requestId;
        m_recentQuestRequestCursor =
            (m_recentQuestRequestCursor + 1) % m_recentQuestRequestIds.size();
    }

    // ------------------------------------------------------------------
    // 阶段20：NPC 可见集合 + Dialogue/Shop 会话 + NPC 请求防重放历史。
    // ------------------------------------------------------------------
    const std::unordered_set<std::uint64_t>& VisibleNpcs() const { return m_visibleNpcs; }
    void AddVisibleNpc(std::uint64_t npcEntityId) { m_visibleNpcs.insert(npcEntityId); }
    bool EraseVisibleNpc(std::uint64_t npcEntityId) { return m_visibleNpcs.erase(npcEntityId) != 0; }
    void ClearVisibleNpcs() { m_visibleNpcs.clear(); }

    // 指令二十一：Dialogue Session（服务器生成 sessionId；30s TTL）。
    const ActiveDialogueSession& DialogueSession() const { return m_dialogueSession; }
    void SetDialogueSession(const ActiveDialogueSession& session) { m_dialogueSession = session; }
    void ClearDialogueSession() { m_dialogueSession = ActiveDialogueSession{}; }
    // 阶段20：每次成功交互刷新活跃时间（TTL 从最近交互起算）。
    void TouchDialogueSession() { m_dialogueSession.openedAt = std::chrono::steady_clock::now(); }

    // 指令四十一：Shop Session（必须经有效 Dialogue Session 打开；30s TTL）。
    const ActiveShopSession& ShopSession() const { return m_shopSession; }
    void SetShopSession(const ActiveShopSession& session) { m_shopSession = session; }
    void ClearShopSession() { m_shopSession = ActiveShopSession{}; }

    // 指令五十七：最近 64 个成功 NPC 请求（Interact/Option/Buy/Sell/Teleport 防重放）。
    bool IsRecentNpcRequest(std::uint64_t requestId) const {
        for (const auto id : m_recentNpcRequestIds) {
            if (id == requestId) {
                return true;
            }
        }
        return false;
    }
    void RememberNpcRequest(std::uint64_t requestId) {
        m_recentNpcRequestIds[m_recentNpcRequestCursor] = requestId;
        m_recentNpcRequestCursor =
            (m_recentNpcRequestCursor + 1) % m_recentNpcRequestIds.size();
    }

    // ------------------------------------------------------------------
    // 阶段21：Portal 可见集合（io 线程权威维护，指令十七）。
    // ------------------------------------------------------------------
    const std::unordered_set<std::uint64_t>& VisiblePortals() const { return m_visiblePortals; }
    void AddVisiblePortal(std::uint64_t portalEntityId) { m_visiblePortals.insert(portalEntityId); }
    bool EraseVisiblePortal(std::uint64_t portalEntityId) {
        return m_visiblePortals.erase(portalEntityId) != 0;
    }
    void ClearVisiblePortals() { m_visiblePortals.clear(); }

    // ------------------------------------------------------------------
    // 阶段21 指令三十一：死亡状态记录（deathMap/X/Y + deadSince 已有）。
    // ------------------------------------------------------------------
    std::uint16_t DeathMapId() const { return m_deathMapId; }
    float DeathX() const { return m_deathX; }
    float DeathY() const { return m_deathY; }
    void MarkDead() {
        m_alive = false;
        m_currentHp = 0;
        m_deadSince = std::chrono::steady_clock::now();
        m_deathMapId = m_mapId; // 指令三十一：死亡位置（复活/观察用）
        m_deathX = m_positionX;
        m_deathY = m_positionY;
        m_respawnProtectedUntil = {}; // 死亡即失去旧保护
    }
    // 指令三十九：复活恢复（HP/Mana 满 + Alive + 状态容器由 WorldServer 清空）。
    void Revive() {
        m_alive = true;
        m_currentHp = m_maxHp;
        m_currentMana = m_maxMana;
        m_deadSince = {};
    }
    // 指令四十五/四十六/四十七：复活保护（runtime flag，非完整 StatusEffect）。
    bool IsRespawnProtected(std::chrono::steady_clock::time_point now) const {
        return now < m_respawnProtectedUntil;
    }
    void SetRespawnProtection(double seconds) {
        m_respawnProtectedUntil =
            std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(seconds));
    }
    void ClearRespawnProtection() { m_respawnProtectedUntil = {}; }

    // ------------------------------------------------------------------
    // 阶段21 指令五十九/六十：地图类请求防重放（PortalUse/Respawn 共用 64 窗口）
    // + 切图并发保护（mapTransitionInProgress）。
    // ------------------------------------------------------------------
    bool IsRecentMapRequest(std::uint64_t requestId) const {
        for (const auto id : m_recentMapRequestIds) {
            if (id == requestId) {
                return true;
            }
        }
        return false;
    }
    void RememberMapRequest(std::uint64_t requestId) {
        m_recentMapRequestIds[m_recentMapRequestCursor] = requestId;
        m_recentMapRequestCursor =
            (m_recentMapRequestCursor + 1) % m_recentMapRequestIds.size();
    }
    bool IsMapTransitionInProgress() const { return m_mapTransitionInProgress; }
    void SetMapTransitionInProgress(bool inProgress) { m_mapTransitionInProgress = inProgress; }

private:
    std::uint64_t m_connectionId = 0;
    std::uint64_t m_accountId = 0;
    std::uint64_t m_characterId = 0;
    std::string m_characterName;
    std::uint16_t m_classId = 0;
    std::uint16_t m_gender = 0;
    std::uint32_t m_level = 1;
    std::uint16_t m_mapId = 1;
    float m_positionX = 0.0f;
    float m_positionY = 0.0f;
    std::uint32_t m_lastProcessedInputSequence = 0;
    bool m_dirtyPosition = false;
    std::chrono::steady_clock::time_point m_lastMoveTime{std::chrono::steady_clock::now()};
    // 阶段12 指令十三：仅 WorldServer io 线程维护。
    std::unordered_set<std::uint64_t> m_visiblePlayers;
    // 阶段13 指令十三：可见怪物集合（仅 io 线程维护）。
    std::unordered_set<std::uint64_t> m_visibleMonsters;
    // 阶段14：战斗字段（runtime only，不持久化，指令八十/八十一）。
    std::uint32_t m_maxHp = kPlayerMaxHp;           // 指令四：100
    std::uint32_t m_currentHp = kPlayerMaxHp;       // 指令四：100
    std::uint32_t m_attackPower = kPlayerAttackPower; // 指令四：20
    std::uint32_t m_defense = kPlayerDefense;       // 指令四：5
    float m_attackRange = kPlayerAttackRange;       // 指令四：100
    float m_attackCooldownSeconds = kPlayerAttackCooldownSeconds; // 指令四：0.8s
    bool m_alive = true;
    // epoch 初始化：首次攻击不受 CD 限制（steady_clock epoch 距 now 极大）。
    std::chrono::steady_clock::time_point m_lastAttackTime{};
    std::chrono::steady_clock::time_point m_deadSince{std::chrono::steady_clock::now()};
    std::uint64_t m_combatTargetEntityId = 0;
    std::array<std::uint64_t, kAttackRequestHistorySize> m_recentAttackRequestIds{};
    std::size_t m_recentAttackRequestCursor = 0;

    // 阶段15：技能字段（runtime only，不持久化——重启/重进恢复默认）。
    std::uint32_t m_maxMana = kPlayerMaxMana;     // 指令十一：100
    std::uint32_t m_currentMana = kPlayerMaxMana; // 指令十一：100
    std::unordered_map<SkillId, std::chrono::steady_clock::time_point>
        m_skillNextReadyTime; // 指令十三：nextReadyTime
    PendingSkillCast m_casting; // 指令十四：同一时间最多一个施法
    std::array<std::uint64_t, kAttackRequestHistorySize> m_recentSkillRequestIds{};
    std::size_t m_recentSkillRequestCursor = 0;

    // 阶段16 指令二十三：Derived 战斗属性（初始 = base；状态变化时重算）。
    std::uint32_t m_effectiveAttackPower = kPlayerAttackPower;
    std::uint32_t m_effectiveDefense = kPlayerDefense;
    float m_effectiveMoveSpeed = kWorldMoveSpeed;
    // 阶段16 指令十四：状态容器（runtime-only）。
    StatusEffectContainer m_statusEffects;

    // 阶段17 指令二：成长数据（level 已在 m_level；exp/gold 服务器权威 + DB 持久化）。
    std::int64_t m_experience = 0;
    std::int64_t m_gold = 0;

    // 阶段18：可见掉落 / 服务器权威背包 / 装备槽（io 线程）。
    std::unordered_set<std::uint64_t> m_visibleItemDrops;
    InventoryContainer m_inventory;
    EquipmentSlots m_equipment;
    std::uint32_t m_equippedWeaponAttack = 0; // 装备攻击加成缓存（Registry 查询结果）
    std::uint32_t m_equippedArmorDefense = 0; // 装备防御加成缓存
    std::array<std::uint64_t, kAttackRequestHistorySize> m_recentItemRequestIds{};
    std::size_t m_recentItemRequestCursor = 0;

    // 阶段19：任务容器 + Quest 请求防重放历史（指令十四/五十九）。
    PlayerQuestContainer m_quests;
    std::array<std::uint64_t, kQuestRequestHistorySize> m_recentQuestRequestIds{};
    std::size_t m_recentQuestRequestCursor = 0;

    // 阶段20：NPC 可见集合（io 线程权威维护）+ Dialogue/Shop 会话 + NPC 请求历史。
    std::unordered_set<std::uint64_t> m_visibleNpcs;
    ActiveDialogueSession m_dialogueSession;
    ActiveShopSession m_shopSession;
    std::array<std::uint64_t, kNpcRequestHistorySize> m_recentNpcRequestIds{};
    std::size_t m_recentNpcRequestCursor = 0;

    // 阶段21：Portal 可见集合 + 死亡记录 + 复活保护 + 切图并发 + 地图请求历史。
    std::unordered_set<std::uint64_t> m_visiblePortals;
    std::uint16_t m_deathMapId = 1;
    float m_deathX = 0.0f;
    float m_deathY = 0.0f;
    std::chrono::steady_clock::time_point m_respawnProtectedUntil{};
    std::array<std::uint64_t, kNpcRequestHistorySize> m_recentMapRequestIds{};
    std::size_t m_recentMapRequestCursor = 0;
    bool m_mapTransitionInProgress = false;
};

} // namespace legend::world
