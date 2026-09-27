#pragma once

#include <cstdint>
#include <vector>

#include "Engine/Combat/DamageEvent.h"
#include "Engine/Combat/DeathEvent.h"
#include "Engine/Entity/EntityId.h"

namespace legend::entity {
class ActorRegistry;
class Character;
}

namespace legend::combat {

// 战斗系统：ValidateAttack / ResolveAttack / ApplyDamage / 死亡入口。
// 统一规则入口——Player 与 Monster 攻击都走这里，禁止各自实现伤害逻辑。
class CombatSystem {
public:
    explicit CombatSystem(legend::entity::ActorRegistry& registry)
        : m_registry(registry) {}

    // 攻击校验：双方战斗存活、目标有效、Feet 距离（DistanceSquared）<= attackRange
    bool ValidateAttack(const legend::entity::Character& attacker,
                        const legend::entity::Character& target) const;

    // 计算并应用一次攻击：公式 -> DamageEvent -> TakeDamage -> 死亡/受击状态。
    // 返回 false 表示攻击无效（校验失败，不产生事件）。
    bool ResolveAttack(legend::entity::Character& attacker,
                       legend::entity::Character& target, DamageEvent& outEvent);

    // 阶段8：技能伤害入口。SkillSystem 只算 Raw（attackSnapshot * multiplier），
    // 防御折减仍由 CombatResolver 统一负责（final = max(1, raw - defense)），
    // 技能不建立第二套伤害公式。射程校验在施法开始时由 SkillSystem 完成（castRange），
    // 此处只校验双方 active + 战斗存活。abilityId 记录来源技能 id。
    bool ApplySkillDamage(legend::entity::Character& attacker,
                          legend::entity::Character& target, float rawDamage,
                          const std::string& abilityId, DamageEvent& outEvent);

    // 应用既有事件（自动测试可直接驱动）：TakeDamage + 死亡/受击状态切换
    bool ApplyDamage(const DamageEvent& event);

    uint64_t GetEventCount() const { return m_sequence; }

    // 本帧产生的伤害事件（供 WorldActorManager 分发仇恨 OnDamaged 等后处理）
    const std::vector<DamageEvent>& GetRecentEvents() const { return m_recentEvents; }
    void ClearRecentEvents() { m_recentEvents.clear(); }

    // 阶段6：本帧产生的死亡事件（Alive -> Dead 一刻各产生一次，上层一次消费）
    const std::vector<DeathEvent>& GetRecentDeaths() const { return m_recentDeaths; }
    void ClearRecentDeaths() { m_recentDeaths.clear(); }

private:
    legend::entity::ActorRegistry& m_registry;
    uint64_t m_sequence = 0;
    std::vector<DamageEvent> m_recentEvents;
    std::vector<DeathEvent> m_recentDeaths;
};

} // namespace legend::combat
