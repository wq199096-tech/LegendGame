#pragma once

#include "Server/WorldServer/Status/ActiveStatusEffect.h"
#include "Server/WorldServer/Status/StatusEffectContainer.h"
#include "Server/WorldServer/Status/StatusEffectRegistry.h"

#include "Shared/Status/StatusEffectTypes.h"

#include <chrono>
#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段16 指令十六：StatusEffectService —— 状态规则纯逻辑（无 IO/无网络）。
// WorldServer 负责编排（施加来源解析、伤害结算、广播、死亡清理）；
// 本服务只负责策略判定与容器变更：
//   ApplyEffect / RefreshEffect / AddStack / RemoveEffect / TickEffects /
//   ExpireEffects / RecalculateDerivedStats（DOT 伤害数值也在此计算，
//   结算由 WorldServer 执行）。
// 时间全部使用 WorldServer steady_clock（权威，指令九十五同源）。
// ---------------------------------------------------------------------------

// ApplyEffect 的入参上下文（纯数据，纯函数可测）。
struct StatusApplyContext {
    StatusEffectId effectId = 0;
    std::uint8_t sourceType = 0;       // CombatEntityType
    std::uint64_t sourceEntityId = 0;
    std::uint32_t sourceSkillId = 0;
    std::uint8_t targetType = 0;       // CombatEntityType
    std::uint64_t targetEntityId = 0;
    bool targetAlive = true;
    bool targetValid = true;           // 目标存在且可被施加
};

// Apply 结果（指令四十九）：result + 本次是否发生了 stack 变化（需要重算属性）。
struct StatusApplyOutcome {
    StatusApplyResult result = StatusApplyResult::UnknownEffect;
    bool stacksChanged = false;   // 新施加/加层/清除 -> 需 RecalculateDerivedStats
    bool durationChanged = false; // 刷新 -> 客户端需要 StatusUpdated
    const ActiveStatusEffect* effect = nullptr;
};

// 指令十六：ApplyEffect —— 统一入口（策略：RefreshDuration / AddStackRefresh /
// Replace 由 Definition 决定，指令四十四~四十八）。
StatusApplyOutcome ApplyEffect(StatusEffectContainer& container,
                               const StatusEffectRegistry& registry,
                               const StatusApplyContext& context,
                               std::uint64_t instanceId,
                               std::chrono::steady_clock::time_point now);

// 指令十六：TickEffects —— 对单个状态结算本轮到期的 DOT Tick。
// 规则（指令四十二/四十三）：now >= nextTick 且 nextTick <= expireTime 才执行；
// 一次调用最多补 kStatusDotMaxCatchUpTicks 跳，超出把 nextTick 推进到未来。
// 返回本次应结算的 DOT 次数（0~3）；每次伤害 = dotDamagePerStack × stacks。
std::uint32_t TickEffect(ActiveStatusEffect& effect, const StatusEffectDefinition& definition,
                         std::chrono::steady_clock::time_point now);

// 指令十六：ExpireEffects —— 移除已到期（now >= expireTime）的状态。
// 返回被移除实例的拷贝（供广播 StatusRemoved(Expired)）。
std::vector<ActiveStatusEffect> ExpireEffects(StatusEffectContainer& container,
                                              std::chrono::steady_clock::time_point now);

// 指令十六/二十五：RecalculateDerivedStats —— 由容器派生有效属性。
//   effectiveAttack  = baseAttack + Σ attackFlat × stacks
//   effectiveDefense = max(0, baseDefense + Σ defenseFlat × stacks)（不能负，指令二十七）
//   effectiveMove    = baseMove × Π moveSpeedMultiplier（乘法叠加，指令二十八）
struct DerivedStats {
    std::uint32_t attackPower = 0;
    std::uint32_t defense = 0;
    float moveSpeed = 0.0f;
};
DerivedStats RecalculateDerivedStats(std::uint32_t baseAttackPower, std::uint32_t baseDefense,
                                     float baseMoveSpeed, const StatusEffectContainer& container,
                                     const StatusEffectRegistry& registry);

// 指令十六：DOT 单跳伤害 = dotDamagePerStack × stacks。
std::uint32_t ComputeDotDamage(const StatusEffectDefinition& definition, std::uint8_t stacks);

} // namespace legend::world
