#pragma once

#include <cstddef>
#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段16：状态效果共享类型（Status Effect Core V0.16）。
// 核心原则：所有状态效果 100% WorldServer 权威——Client 不能决定是否有 Buff/
// 持续时间/剩余时间/层数/属性加成/DOT 伤害/减速倍率/什么时候结束；Client 只能
// 接收 StatusApplied/Updated/Removed/Snapshot 并做展示。
// ---------------------------------------------------------------------------

// 指令三：StatusEffectId（uint32_t；阶段16 固定五个）。
using StatusEffectId = std::uint32_t;

inline constexpr StatusEffectId kStatusEffectIdBattleFocus = 2001; // Battle Focus
inline constexpr StatusEffectId kStatusEffectIdArmorBreak = 2002;  // Armor Break
inline constexpr StatusEffectId kStatusEffectIdBurn = 2003;        // Burn
inline constexpr StatusEffectId kStatusEffectIdPoison = 2004;      // Poison
inline constexpr StatusEffectId kStatusEffectIdSlow = 2005;        // Slow

// 指令四：StatusEffectCategory。
enum class StatusEffectCategory : std::uint8_t {
    Buff = 1,
    Debuff = 2,
};

// 指令五：StatusEffectStackPolicy。
enum class StatusEffectStackPolicy : std::uint8_t {
    RefreshDuration = 1,   // 不叠层，重复施加刷新持续时间
    AddStackRefresh = 2,   // 重复施加增加层数并刷新持续时间
    Replace = 3,           // 替换（阶段16 未使用，协议预留）
};

// 指令四十九：StatusApplyResult。
enum class StatusApplyResult : std::uint8_t {
    Applied = 0,                // 新施加
    Refreshed = 1,              // 刷新持续时间（ stacks 不变）
    StackAdded = 2,             // 层数 +1
    AtMaxStacksRefreshed = 3,   // 已满层，仅刷新持续时间
    InvalidTarget = 4,
    TargetDead = 5,
    UnknownEffect = 6,
};

inline const char* StatusApplyResultName(std::uint8_t result) {
    switch (static_cast<StatusApplyResult>(result)) {
        case StatusApplyResult::Applied: return "Applied";
        case StatusApplyResult::Refreshed: return "Refreshed";
        case StatusApplyResult::StackAdded: return "StackAdded";
        case StatusApplyResult::AtMaxStacksRefreshed: return "AtMaxStacksRefreshed";
        case StatusApplyResult::InvalidTarget: return "InvalidTarget";
        case StatusApplyResult::TargetDead: return "TargetDead";
        case StatusApplyResult::UnknownEffect: return "UnknownEffect";
    }
    return "Unknown";
}

// 指令五十：StatusRemovedReason。
enum class StatusRemovedReason : std::uint8_t {
    Expired = 0,
    Dispelled = 1,      // 阶段16 无主动 Dispel，协议预留
    TargetDied = 2,
    ServerCleanup = 3,
    Replaced = 4,
};

inline const char* StatusRemovedReasonName(std::uint8_t reason) {
    switch (static_cast<StatusRemovedReason>(reason)) {
        case StatusRemovedReason::Expired: return "Expired";
        case StatusRemovedReason::Dispelled: return "Dispelled";
        case StatusRemovedReason::TargetDied: return "TargetDied";
        case StatusRemovedReason::ServerCleanup: return "ServerCleanup";
        case StatusRemovedReason::Replaced: return "Replaced";
    }
    return "Unknown";
}

// 指令五十六：每个实体的状态 Snapshot 数量上限（Encode/Decode 超过拒绝）。
inline constexpr std::size_t kStatusEffectMaxSnapshotCount = 32;

// 指令四十三：单次 Status Tick 对同一状态的 DOT 补跳上限。
inline constexpr std::uint32_t kStatusDotMaxCatchUpTicks = 3;

} // namespace legend::world
