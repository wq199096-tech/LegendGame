#include "Server/WorldServer/Status/StatusEffectService.h"

#include <algorithm>

namespace legend::world {

StatusApplyOutcome ApplyEffect(StatusEffectContainer& container,
                               const StatusEffectRegistry& registry,
                               const StatusApplyContext& context,
                               std::uint64_t instanceId,
                               std::chrono::steady_clock::time_point now) {
    StatusApplyOutcome outcome;
    const StatusEffectDefinition* definition = registry.FindEffect(context.effectId);
    if (!definition) {
        outcome.result = StatusApplyResult::UnknownEffect; // 未知效果
        return outcome;
    }
    if (!context.targetValid) {
        outcome.result = StatusApplyResult::InvalidTarget;
        return outcome;
    }
    if (!context.targetAlive) {
        outcome.result = StatusApplyResult::TargetDead; // 指令一百一十二：不能进 Container
        return outcome;
    }

    const auto expireTime = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                     std::chrono::milliseconds(definition->durationMs));
    auto* existing = container.Find(context.effectId);
    if (!existing) {
        // 指令四十九：新施加。stacks = min(1, maxStacks)；DOT 首跳 = now + tickInterval。
        const std::uint8_t stacks =
            definition->maxStacks > 0 ? static_cast<std::uint8_t>(1) : static_cast<std::uint8_t>(0);
        ActiveStatusEffect effect(instanceId, context.effectId, context.sourceType,
                                  context.sourceEntityId, context.sourceSkillId,
                                  context.targetType, context.targetEntityId, stacks, now,
                                  expireTime,
                                  now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                           std::chrono::milliseconds(definition->tickIntervalMs)));
        auto& stored = container.Apply(effect);
        outcome.result = StatusApplyResult::Applied;
        outcome.stacksChanged = true;
        outcome.effect = &stored;
        return outcome;
    }

    // 已存在：按 Definition 的 stackPolicy 处理（指令四十四~四十八）。
    switch (definition->stackPolicy) {
        case StatusEffectStackPolicy::RefreshDuration: {
            // 指令四十四/四十六/四十八：stacks 不变，expireTime 刷新；
            // 有 DOT 的（Burn）nextTick 重新从 now + tickInterval 开始。
            existing->SetExpireTime(expireTime);
            if (definition->tickIntervalMs > 0) {
                existing->SetNextTickTime(
                    now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::milliseconds(definition->tickIntervalMs)));
            }
            existing->SetSource(context.sourceType, context.sourceEntityId,
                                context.sourceSkillId); // 指令七十二：最新来源覆盖
            outcome.result = StatusApplyResult::Refreshed;
            outcome.durationChanged = true;
            outcome.effect = existing;
            return outcome;
        }
        case StatusEffectStackPolicy::AddStackRefresh: {
            if (existing->Stacks() < definition->maxStacks) {
                // 指令四十五/四十七：层数 +1、expireTime 刷新、nextTick 不重置。
                existing->SetStacks(static_cast<std::uint8_t>(existing->Stacks() + 1));
                existing->SetExpireTime(expireTime);
                existing->SetSource(context.sourceType, context.sourceEntityId,
                                    context.sourceSkillId);
                outcome.result = StatusApplyResult::StackAdded;
                outcome.stacksChanged = true;
                outcome.effect = existing;
                return outcome;
            }
            // 指令四十五/一百：已满层 -> 仍刷新 duration（stacks/nextTick 不变）。
            existing->SetExpireTime(expireTime);
            existing->SetSource(context.sourceType, context.sourceEntityId,
                                context.sourceSkillId);
            outcome.result = StatusApplyResult::AtMaxStacksRefreshed;
            outcome.durationChanged = true;
            outcome.effect = existing;
            return outcome;
        }
        case StatusEffectStackPolicy::Replace:
        default: {
            // 指令五（Replace 预留）：整体替换为新实例。
            ActiveStatusEffect effect(instanceId, context.effectId, context.sourceType,
                                      context.sourceEntityId, context.sourceSkillId,
                                      context.targetType, context.targetEntityId,
                                      static_cast<std::uint8_t>(1), now, expireTime,
                                      now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                               std::chrono::milliseconds(definition->tickIntervalMs)));
            auto& stored = container.Apply(effect);
            outcome.result = StatusApplyResult::Refreshed;
            outcome.stacksChanged = true;
            outcome.effect = &stored;
            return outcome;
        }
    }
}

std::uint32_t TickEffect(ActiveStatusEffect& effect, const StatusEffectDefinition& definition,
                         std::chrono::steady_clock::time_point now) {
    if (definition.tickIntervalMs == 0 || definition.dotDamagePerStack == 0) {
        return 0; // 非 DOT 状态
    }
    const auto tickInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::milliseconds(definition.tickIntervalMs));
    std::uint32_t ticks = 0;
    // 指令四十二：now >= nextTick 且 nextTick <= expireTime 才执行；
    // 指令四十三：一次最多补 kStatusDotMaxCatchUpTicks 跳。
    while (ticks < kStatusDotMaxCatchUpTicks && now >= effect.NextTickTime() &&
           effect.NextTickTime() <= effect.ExpireTime()) {
        ++ticks;
        effect.SetNextTickTime(effect.NextTickTime() + tickInterval);
    }
    if (ticks == kStatusDotMaxCatchUpTicks && now >= effect.NextTickTime()) {
        // 指令四十三：仍严重滞后 -> 把 nextTick 推进到未来，避免瞬间补几十跳。
        effect.SetNextTickTime(now + tickInterval);
    }
    return ticks;
}

std::vector<ActiveStatusEffect> ExpireEffects(StatusEffectContainer& container,
                                              std::chrono::steady_clock::time_point now) {
    // 指令十六/七十八：到期移除，绝不能到期后继续 DOT。
    std::vector<ActiveStatusEffect> expired;
    for (auto& [effectId, effect] : container.All()) {
        if (now >= effect.ExpireTime()) {
            expired.push_back(effect);
        }
    }
    for (const auto& effect : expired) {
        container.Remove(effect.EffectId());
    }
    return expired;
}

DerivedStats RecalculateDerivedStats(std::uint32_t baseAttackPower, std::uint32_t baseDefense,
                                     float baseMoveSpeed, const StatusEffectContainer& container,
                                     const StatusEffectRegistry& registry) {
    // 指令二十五/二十六/二十七/二十八：仅在 Applied/stack 变化/Removed/Expired 时调用。
    std::int64_t attack = static_cast<std::int64_t>(baseAttackPower);
    std::int64_t defense = static_cast<std::int64_t>(baseDefense);
    float moveSpeed = baseMoveSpeed;
    for (const auto& [effectId, effect] : container.All()) {
        const StatusEffectDefinition* definition = registry.FindEffect(effectId);
        if (!definition) {
            continue;
        }
        attack += static_cast<std::int64_t>(definition->attackFlatModifier) * effect.Stacks();
        defense += static_cast<std::int64_t>(definition->defenseFlatModifier) * effect.Stacks();
        moveSpeed *= definition->moveSpeedMultiplier; // 指令二十八：乘法叠加
    }
    DerivedStats stats;
    stats.attackPower = static_cast<std::uint32_t>(std::max<std::int64_t>(0, attack));
    stats.defense =
        static_cast<std::uint32_t>(std::max<std::int64_t>(0, defense)); // 指令二十七：不能负
    stats.moveSpeed = std::max(0.0f, moveSpeed);
    return stats;
}

std::uint32_t ComputeDotDamage(const StatusEffectDefinition& definition, std::uint8_t stacks) {
    // 指令三十五：DOT 伤害服务器权威；每层叠加（Poison 3 层 = 12/秒）。
    return definition.dotDamagePerStack * stacks;
}

} // namespace legend::world
