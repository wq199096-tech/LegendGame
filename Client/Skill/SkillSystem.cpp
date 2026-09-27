#include "Client/Skill/SkillSystem.h"

#include <algorithm>
#include <cmath>

#include "Client/Character/PlayerCharacter.h"
#include "Client/World/MonsterCharacter.h"
#include "Engine/Combat/CombatSystem.h"
#include "Engine/Combat/CombatTarget.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Entity/ActorRegistry.h"
#include "Engine/Skill/SkillResource.h"

namespace legend::skill {

void SkillSystem::Initialize(const SkillDatabase* database,
                             const legend::entity::ActorRegistry* registry,
                             legend::combat::CombatSystem* combat) {
    m_database = database;
    m_registry = registry;
    m_combat = combat;
    m_context.Clear();
    m_lastHit = LastHitInfo{};
}

SkillCastResult SkillSystem::CanCast(const PlayerCharacter& player, const std::string& skillId,
                                     const legend::combat::CombatTarget& target,
                                     const SkillCooldowns& cooldowns) const {
    // 阶段8.1指令十六：最前置 Active 校验——Inactive Player 绝不能施法
    //（CombatSystem::ApplySkillDamage 的 attacker/target IsActive 双保险继续保留，两层防线）
    if (!player.IsActive()) {
        return {false, "player inactive"};
    }
    // 顺序（阶段8指令三十四）：Skill存在 -> 状态允许 -> CD -> Mana -> 目标 -> 距离
    if (m_database == nullptr || m_registry == nullptr || m_combat == nullptr) {
        return {false, "skill system not initialized"};
    }
    const SkillDefinition* definition = m_database->Get(skillId);
    if (definition == nullptr) {
        return {false, "unknown skill '" + skillId + "'"};
    }
    if (!player.IsCombatAlive()) {
        return {false, "player dead or combat disabled"};
    }
    if (player.GetActionState() != legend::entity::CharacterActionState::Normal) {
        return {false, "action state blocks casting"}; // Attacking/HitReact/SkillCasting/Dead
    }
    if (!cooldowns.IsReady(skillId)) {
        return {false, "skill on cooldown"};
    }
    if (!player.GetSkillResource().CanSpend(definition->manaCost)) {
        return {false, "not enough mana"};
    }
    if (definition->targetType == SkillTargetType::SingleTarget) {
        // SingleTarget 必须有有效目标（复用 PlayerCombatController 的 CombatTarget，
        // 不建立第二套目标系统）
        const auto* targetActor = target.Resolve(*m_registry);
        if (targetActor == nullptr) {
            return {false, "no target"};
        }
        if (targetActor->GetActorType() != legend::entity::ActorType::Monster) {
            return {false, "target is not a monster"};
        }
        // 距离语义：统一 Feet Position + DistanceSquared（指令三十五）
        const float distSq = (targetActor->GetPosition() - player.GetPosition()).LengthSq();
        if (distSq > definition->castRange * definition->castRange) {
            return {false, "target out of cast range"};
        }
    }
    return {true, ""};
}

SkillCastResult SkillSystem::BeginCast(PlayerCharacter& player, const std::string& skillId,
                                       const legend::combat::CombatTarget& target,
                                       SkillCooldowns& cooldowns) {
    // 先纯校验：任何失败不扣 MP、不启动 CD（指令二十一）
    SkillCastResult can = CanCast(player, skillId, target, cooldowns);
    if (!can.success) {
        return can;
    }
    const SkillDefinition* definition = m_database->Get(skillId);

    // ---- 全部通过：一次性应用施法代价与状态 ----
    (void)player.GetSkillResource().Spend(definition->manaCost);
    cooldowns.StartCooldown(skillId, definition->cooldown);

    // SingleTarget：施法开始朝向目标并锁定方向（指令二十九）；SelfArea 保持当前朝向
    legend::entity::EntityId targetId = legend::entity::kInvalidEntityId;
    if (definition->targetType == SkillTargetType::SingleTarget) {
        const auto* targetActor = target.Resolve(*m_registry);
        if (targetActor != nullptr) {
            targetId = targetActor->GetId();
            const math::Vector2 delta = targetActor->GetPosition() - player.GetPosition();
            player.SetDirection(legend::entity::DirectionFromVector(delta, player.GetDirection()));
        }
    }

    // Context：attackSnapshot 锁定施法开始时的 Final Attack（指令七十五/七十六）
    m_context.Clear();
    m_context.skillId = skillId;
    m_context.casterId = player.GetId();
    m_context.targetId = targetId;
    m_context.lockedDirection = player.GetDirection();
    m_context.attackSnapshot = player.GetCombatStats().attack;
    m_context.started = true;
    m_context.eventConsumed = false;
    m_context.definition = *definition;

    player.SetActionClipOverride(definition->animation);
    player.EnterSkillCasting();
    // 显式停止当前动画：下一次 UpdateAnimation 的 Play(技能Clip) 从头播放。
    // 不能依赖 Play 的同名保持语义——同一技能连续施放时（中间无其它 Clip），
    // 上一次已播完的 Clip 会被保持 finished 状态，事件永不触发（阶段8实测）。
    player.GetAnimationPlayer().Stop();

    LOG_INFO("[Skill] Begin " + skillId +
             (targetId != legend::entity::kInvalidEntityId
                  ? " target=" + std::to_string(targetId)
                  : std::string(" target=none")) +
             " mana=" + std::to_string(static_cast<int>(player.GetSkillResource().GetMana())) +
             " cd=" + std::to_string(definition->cooldown));
    return {true, ""};
}

void SkillSystem::HandleAnimationEvent(PlayerCharacter& player, const std::string& eventName) {
    if (!m_context.IsActive() || m_context.eventConsumed) {
        return; // 无施法 / 事件已消费：exactly-once（指令三十二）
    }
    // 比较定义的 animationEvent，不在代码里硬编码 "skill_hit"（指令八十九）
    if (eventName != m_context.definition.animationEvent) {
        return;
    }
    m_context.eventConsumed = true; // 一次施法最多结算一次，先标记再结算
    ResolveSkillHit(player);
}

void SkillSystem::ResolveSkillHit(PlayerCharacter& player) {
    if (m_database == nullptr || m_registry == nullptr || m_combat == nullptr) {
        return;
    }
    const SkillDefinition& definition = m_context.definition;
    const float rawDamage = m_context.attackSnapshot * definition.damageMultiplier;
    m_lastHit.valid = true;
    m_lastHit.abilityId = m_context.skillId;
    m_lastHit.rawDamage = rawDamage;
    m_lastHit.hitCount = 0;

    if (definition.targetType == SkillTargetType::SingleTarget) {
        // 事件时刻重新 Resolve：目标死亡 / Despawn / inactive 安全失败（指令六十九/七十）
        legend::entity::Character* target = m_registry->Get(m_context.targetId);
        if (target == nullptr || !target->IsActive() ||
            target->GetActorType() != legend::entity::ActorType::Monster) {
            LOG_INFO("[Skill] " + m_context.skillId +
                     " hit missed (target gone or not a monster).");
            return;
        }
        legend::combat::DamageEvent event;
        if (m_combat->ApplySkillDamage(player, *target, rawDamage, m_context.skillId, event)) {
            m_lastHit.finalDamage = event.finalDamage;
            m_lastHit.hitCount = 1;
            LOG_INFO("[Skill] " + m_context.skillId + " hit target=" +
                     std::to_string(m_context.targetId) + " raw=" + std::to_string(rawDamage));
        }
        return;
    }

    // SelfArea：以施法者 Feet 为中心 FindInRadius（指令三十六/九十一），
    // 筛选 Active + 战斗存活 + Monster（AOE 不能打 NPC / Player / 尸体）
    std::vector<legend::entity::Character*> candidates =
        m_registry->FindInRadius(player.GetPosition(), definition.aoeRadius);
    std::vector<legend::entity::Character*> targets;
    for (legend::entity::Character* actor : candidates) {
        if (actor == nullptr || !actor->IsActive() || !actor->IsCombatAlive() ||
            actor->GetActorType() != legend::entity::ActorType::Monster) {
            continue;
        }
        targets.push_back(actor);
    }
    // 按 EntityId 排序：自动测试日志顺序稳定（指令九十二）
    std::sort(targets.begin(), targets.end(),
              [](const legend::entity::Character* a, const legend::entity::Character* b) {
                  return a->GetId() < b->GetId();
              });
    for (legend::entity::Character* target : targets) {
        legend::combat::DamageEvent event;
        if (m_combat->ApplySkillDamage(player, *target, rawDamage, m_context.skillId, event)) {
            ++m_lastHit.hitCount;
            if (m_lastHit.hitCount == 1) {
                m_lastHit.finalDamage = event.finalDamage;
            }
        }
    }
    LOG_INFO("[Skill] " + m_context.skillId + " hitCount=" + std::to_string(m_lastHit.hitCount));
}

void SkillSystem::CancelCast(const std::string& reason) {
    if (!m_context.IsActive()) {
        return;
    }
    LOG_INFO("[Skill] " + m_context.skillId + " canceled: " + reason +
             " (mana not refunded, cooldown continues).");
    m_context.Clear(); // 未触发的 skill_hit 一并取消；Mana/CD 保持（指令九十/二十五）
}

void SkillSystem::FinishCast(PlayerCharacter& player) {
    if (!m_context.IsActive()) {
        return;
    }
    const std::string skillId = m_context.skillId;
    m_context.Clear();
    player.ReturnToNormal(); // SkillCasting -> Normal（同时清除 Clip 覆盖）
    LOG_INFO("[Skill] " + skillId + " cast finished.");
}

} // namespace legend::skill
