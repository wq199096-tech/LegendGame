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

bool CombatSystem::ApplyDamage(const DamageEvent& event) {
    legend::entity::Character* target = m_registry.Get(event.targetId);
    if (target == nullptr) {
        return false; // 目标已不存在（despawn），事件作废
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
        LOG_INFO("[Death] entity " + std::to_string(event.targetId));
    } else {
        // 受击：打断当前攻击，进入 HitReact（动画结束后由控制器恢复 Normal）
        target->EnterHitReact();
    }
    return true;
}

} // namespace legend::combat
