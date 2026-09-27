#include "Engine/Combat/CombatSystem.h"

#include <cmath>

#include "Engine/Combat/CombatResolver.h"
#include "Engine/Combat/CombatStats.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/ActorRegistry.h"
#include "Engine/Entity/Character.h"

namespace legend::combat {

bool CombatSystem::ValidateAttack(const legend::entity::Character& attacker,
                                  const legend::entity::Character& target) const {
    // 严格校验：inactive Actor 绝不能攻击 / 被攻击 / 产生 DamageEvent
    if (!attacker.IsActive() || !target.IsActive()) {
        return false;
    }
    // 战斗存活 = combat enabled + HP > 0（尸体与 NPC 不参战）
    if (!attacker.IsCombatAlive() || !target.IsCombatAlive()) {
        return false;
    }
    // Feet 距离（DistanceSquared）<= attackRange
    const math::Vector2 delta = target.GetPosition() - attacker.GetPosition();
    const float range = attacker.GetCombatStats().attackRange;
    return delta.LengthSq() <= range * range;
}

bool CombatSystem::ResolveAttack(legend::entity::Character& attacker,
                                 legend::entity::Character& target, DamageEvent& outEvent) {
    if (!ValidateAttack(attacker, target)) {
        return false;
    }
    const CombatStats& attackerStats = attacker.GetCombatStats();
    const CombatStats& targetStats = target.GetCombatStats();

    DamageEvent event;
    event.sourceId = attacker.GetId();
    event.targetId = target.GetId();
    event.rawDamage = CombatResolver::ComputeRawDamage(attackerStats.attack,
                                                       targetStats.defense);
    event.finalDamage = CombatResolver::ComputeFinalDamage(attackerStats.attack,
                                                           targetStats.defense);
    event.sequence = ++m_sequence;

    outEvent = event;
    return ApplyDamage(event);
}

bool CombatSystem::ApplySkillDamage(legend::entity::Character& attacker,
                                    legend::entity::Character& target, float rawDamage,
                                    const std::string& abilityId, DamageEvent& outEvent) {
    // 阶段5.1 同源防线：inactive Actor 绝不能攻击 / 被攻击
    if (!attacker.IsActive() || !target.IsActive()) {
        return false;
    }
    if (!attacker.IsCombatAlive() || !target.IsCombatAlive()) {
        return false; // 施法者死亡 / 目标已死：技能落空
    }
    DamageEvent event;
    event.sourceId = attacker.GetId();
    event.targetId = target.GetId();
    event.rawDamage = rawDamage;
    event.finalDamage = CombatResolver::ComputeFinalDamage(rawDamage,
                                                           target.GetCombatStats().defense);
    event.abilityId = abilityId;
    event.sequence = ++m_sequence;
    outEvent = event;
    return ApplyDamage(event);
}

bool CombatSystem::ApplyDamage(const DamageEvent& event) {
    legend::entity::Character* target = m_registry.Get(event.targetId);
    // 阶段5.1：inactive Actor 绝不能被攻击（即使绕过 ValidateAttack 直接调用）
    if (target == nullptr || !target->IsActive()) {
        return false; // 目标已不存在（despawn）或 inactive，事件作废
    }
    const float applied = target->GetCombatStats().TakeDamage(event.finalDamage);
    if (applied <= 0.0f) {
        return false; // 目标已死，不可再次受击
    }
    m_recentEvents.push_back(event); // 供上层分发仇恨等后处理
    LOG_INFO("[Damage] " + std::to_string(event.sourceId) + " -> " +
             std::to_string(event.targetId) + " : " + std::to_string(applied));

    if (!target->GetCombatStats().IsAlive()) {
        // 死亡入口：进入 Dead（active 保持 true 以播放死亡动画，alive=false）
        target->EnterDead();
        // 阶段6：Alive -> Dead 这一刻产生一次 DeathEvent（HP>0 -> 0 的那次伤害；
        // 已死目标 TakeDamage 返回 0 提前返回，天然防止每帧重复产生）
        DeathEvent death;
        death.victimId = target->GetId();
        death.killerId = event.sourceId;
        death.position = target->GetPosition();
        death.sequence = ++m_sequence;
        m_recentDeaths.push_back(death);
        LOG_INFO("[Death] entity " + std::to_string(event.targetId));
    } else {
        // 受击：打断当前攻击，进入 HitReact（动画结束后由控制器恢复 Normal）
        target->EnterHitReact();
    }
    return true;
}

} // namespace legend::combat
