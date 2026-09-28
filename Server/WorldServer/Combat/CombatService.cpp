#include "Server/WorldServer/Combat/CombatService.h"

#include <chrono>

namespace legend::world {

bool IsAttackOffCooldown(std::chrono::steady_clock::time_point now,
                         std::chrono::steady_clock::time_point lastAttackTime,
                         float attackCooldownSeconds) {
    const float elapsed =
        std::chrono::duration<float>(now - lastAttackTime).count();
    return elapsed >= attackCooldownSeconds;
}

bool IsWithinAttackRange(float distanceSquared, float attackRangeSquared) {
    return distanceSquared <= attackRangeSquared;
}

CombatResultCode ValidateAttack(const AttackContext& context) {
    // 指令三十七：验证链——顺序即职责清单（ValidateAttack/Cooldown/Distance）。
    if (!context.attackerAlive) {
        return CombatResultCode::AttackerDead; // 指令二十二：死亡不能攻击
    }
    if (!context.attackerInWorld) {
        return CombatResultCode::NotInWorld;
    }
    if (!context.targetAlive) {
        return CombatResultCode::TargetDead; // 指令二十一：alive=false 拒绝再次攻击
    }
    if (!context.targetVisible) {
        return CombatResultCode::InvalidTarget; // 指令三十二/三十三：防远程作弊
    }
    if (!context.sameMap) {
        return CombatResultCode::DifferentMap; // 指令二十：跨地图绝不能攻击
    }
    if (!IsWithinAttackRange(context.distanceSquared, context.attackRangeSquared)) {
        return CombatResultCode::OutOfRange; // 指令十八
    }
    if (!IsAttackOffCooldown(context.now, context.lastAttackTime,
                             context.attackCooldownSeconds)) {
        return CombatResultCode::Cooldown; // 指令二十九：过快返回 Cooldown 不造成伤害
    }
    return CombatResultCode::Success;
}

} // namespace legend::world
