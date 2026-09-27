#include "Client/Combat/MonsterCombatController.h"

#include "Client/World/MonsterCharacter.h"
#include "Engine/Combat/CombatSystem.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/ActorRegistry.h"
#include "Engine/Entity/Character.h"

namespace legend::world {

void MonsterCombatController::Update(MonsterCharacter& monster,
                                     const legend::entity::ActorRegistry& registry,
                                     legend::combat::CombatSystem& combat, float deltaTime) {
    monster.TickCooldown(deltaTime);

    switch (monster.GetActionState()) {
    case legend::entity::CharacterActionState::Attacking: {
        ConsumeAttackEvent(monster, registry, combat);
        // NonLoop 攻击动画播完 -> 恢复 Normal（用动画长度，不写死 Timer）
        if (monster.GetAnimationPlayer().IsFinished()) {
            monster.ReturnToNormal();
        }
        break;
    }
    case legend::entity::CharacterActionState::HitReact: {
        // 受击硬直：丢弃遗留攻击事件（攻击被打断不再产生伤害）
        (void)monster.GetAnimationPlayer().ConsumeEvents();
        if (monster.GetAnimationPlayer().IsFinished()) {
            monster.ReturnToNormal();
        }
        break;
    }
    case legend::entity::CharacterActionState::Dead:
    case legend::entity::CharacterActionState::Normal:
    default:
        break;
    }
}

bool MonsterCombatController::RequestAttack(MonsterCharacter& monster,
                                            const legend::entity::ActorRegistry& registry,
                                            legend::combat::CombatSystem& combat) {
    if (!monster.IsCombatAlive() ||
        monster.GetActionState() != legend::entity::CharacterActionState::Normal) {
        return false; // 已死 / 正在攻击或受击
    }
    if (monster.GetAttackCooldownRemaining() > 0.0f) {
        return false; // 冷却中
    }
    const auto* target = monster.GetTargetHandle().Resolve(registry);
    if (target == nullptr || !target->IsCombatAlive()) {
        return false; // 目标无效/已死
    }
    // 攻击距离：Feet 距离（DistanceSquared）<= attackRange
    const math::Vector2 delta = target->GetPosition() - monster.GetPosition();
    const float range = monster.GetCombatStats().attackRange;
    if (delta.LengthSq() > range * range) {
        return false; // 超出攻击距离不造成伤害
    }
    // 攻击开始：面向目标锁定方向（DirectionFromVector）
    monster.SetDirection(legend::entity::DirectionFromVector(delta, monster.GetDirection()));
    monster.EnterAttacking();
    // 冷却在攻击成功开始时置为 attackInterval（不是命中后才开 CD）
    monster.SetAttackCooldownRemaining(monster.GetCombatStats().attackInterval);
    LOG_INFO("[Combat] " + monster.GetName() + "#" + std::to_string(monster.GetId()) +
             " attacks " + target->GetName() + "#" + std::to_string(target->GetId()));
    return true;
}

void MonsterCombatController::ConsumeAttackEvent(
    MonsterCharacter& monster, const legend::entity::ActorRegistry& registry,
    legend::combat::CombatSystem& combat) {
    auto events = monster.GetAnimationPlayer().ConsumeEvents();
    for (const auto& eventName : events) {
        if (eventName != "attack_hit") {
            continue;
        }
        // 事件时刻重新解析目标：防止延迟一帧后目标已删除/死亡
        auto* target = monster.GetTargetHandle().Resolve(registry);
        if (target == nullptr) {
            continue; // 目标已失效：本次攻击落空
        }
        legend::combat::DamageEvent event;
        combat.ResolveAttack(monster, *target, event);
        // 目标死亡/受击状态由 CombatSystem::ApplyDamage 统一处理
    }
}

} // namespace legend::world