#pragma once

#include "Shared/Status/StatusEffectTypes.h"

#include <cstdint>
#include <string>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段16 指令六：StatusEffectDefinition —— 状态效果静态配置。
// 阶段16 硬编码五个（StatusEffectRegistry），不上 JSON、不进数据库（runtime-only）。
// 属性修饰规则（指令二十六~二十八）：
//   effectiveAttack  = baseAttack  + Σ attackFlatModifier × stacks
//   effectiveDefense = max(0, baseDefense + Σ defenseFlatModifier × stacks)
//   effectiveMove    = baseMoveSpeed × Π moveSpeedMultiplier
// ---------------------------------------------------------------------------

struct StatusEffectDefinition {
    StatusEffectId effectId = 0;
    std::string name;
    StatusEffectCategory category = StatusEffectCategory::Debuff;
    std::uint32_t durationMs = 0;        // 持续时间（0 = 永久，阶段16 无此用法）
    std::uint32_t tickIntervalMs = 0;    // DOT tick 间隔（0 = 无 DOT）
    std::uint8_t maxStacks = 1;
    StatusEffectStackPolicy stackPolicy = StatusEffectStackPolicy::RefreshDuration;
    std::int32_t attackFlatModifier = 0;   // 每层攻击平坦加成（可为负）
    std::int32_t defenseFlatModifier = 0;  // 每层防御平坦加成（可为负）
    float moveSpeedMultiplier = 1.0f;      // 移速乘数（乘法叠加）
    std::uint32_t dotDamagePerStack = 0;   // 每层每 Tick DOT 伤害
};

// 指令七：Battle Focus —— Buff，Attack +10，10s，不叠层，重复刷新。
inline const StatusEffectDefinition kBattleFocusDefinition{
    kStatusEffectIdBattleFocus, "Battle Focus", StatusEffectCategory::Buff,
    10000u, 0u, 1u, StatusEffectStackPolicy::RefreshDuration,
    10, 0, 1.0f, 0u,
};

// 指令八：Armor Break —— Debuff，Defense -2/层，8s，最多 3 层。
inline const StatusEffectDefinition kArmorBreakDefinition{
    kStatusEffectIdArmorBreak, "Armor Break", StatusEffectCategory::Debuff,
    8000u, 0u, 3u, StatusEffectStackPolicy::AddStackRefresh,
    0, -2, 1.0f, 0u,
};

// 指令九：Burn —— Debuff/DOT，每 2s 8 伤害，8s 共 4 Tick，不叠层。
inline const StatusEffectDefinition kBurnDefinition{
    kStatusEffectIdBurn, "Burn", StatusEffectCategory::Debuff,
    8000u, 2000u, 1u, StatusEffectStackPolicy::RefreshDuration,
    0, 0, 1.0f, 8u,
};

// 指令十：Poison —— Debuff/DOT，每层每 1s 4 伤害，6s 共 6 Tick，最多 3 层。
inline const StatusEffectDefinition kPoisonDefinition{
    kStatusEffectIdPoison, "Poison", StatusEffectCategory::Debuff,
    6000u, 1000u, 3u, StatusEffectStackPolicy::AddStackRefresh,
    0, 0, 1.0f, 4u,
};

// 指令十一：Slow —— Debuff，moveSpeed ×0.6，5s，不叠层。
inline const StatusEffectDefinition kSlowDefinition{
    kStatusEffectIdSlow, "Slow", StatusEffectCategory::Debuff,
    5000u, 0u, 1u, StatusEffectStackPolicy::RefreshDuration,
    0, 0, 0.6f, 0u,
};

} // namespace legend::world
