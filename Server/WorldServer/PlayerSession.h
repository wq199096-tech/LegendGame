#pragma once

#include "Shared/Combat/CombatTypes.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/Skill/SkillDefinition.h"
#include "Shared/Skill/SkillTypes.h"

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
    // 阶段14 指令四：固定基础战斗属性（不做装备/成长加成）。
    // ------------------------------------------------------------------
    std::uint32_t MaxHp() const { return m_maxHp; }
    std::uint32_t CurrentHp() const { return m_currentHp; }
    std::uint32_t AttackPower() const { return m_attackPower; }
    std::uint32_t Defense() const { return m_defense; }
    float AttackRange() const { return m_attackRange; }
    float AttackCooldownSeconds() const { return m_attackCooldownSeconds; }
    bool Alive() const { return m_alive; }

    // 指令二十四：扣血（不低于 0；返回是否致死）。
    bool ApplyDamage(std::uint32_t damage) {
        m_currentHp = (m_currentHp > damage) ? (m_currentHp - damage) : 0u;
        if (m_currentHp == 0 && m_alive) {
            m_alive = false;
            return true;
        }
        return false;
    }
    // 指令七十九：死亡（不自动复活）。
    void MarkDead() {
        m_alive = false;
        m_currentHp = 0;
        m_deadSince = std::chrono::steady_clock::now();
    }
    // 指令七十九：死亡时刻。
    std::chrono::steady_clock::time_point DeadSince() const { return m_deadSince; }

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
};

} // namespace legend::world
