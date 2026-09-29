#pragma once

#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段15：技能与施法共享类型（Skill & Ability Replication Core V0.15）。
// 核心原则（指令一）：技能效果 100% WorldServer 权威——Client 只表达
// "我要释放哪个技能，以及目标是谁"；伤害/命中/冷却/Mana/施法完成时间/AOE
// 命中目标/死亡全部由服务器决定（指令二）。
// ---------------------------------------------------------------------------

// 指令四：SkillId（uint32_t；阶段15 固定三个测试技能）。
using SkillId = std::uint32_t;

inline constexpr SkillId kSkillIdQuickStrike = 1001; // Quick Strike
inline constexpr SkillId kSkillIdFireBolt = 1002;    // Fire Bolt
inline constexpr SkillId kSkillIdWhirlwind = 1003;   // Whirlwind

// 指令五：SkillTargetType（阶段15 实际只用 Monster / Self）。
enum class SkillTargetType : std::uint8_t {
    None = 0,
    Monster = 1,
    Ground = 2,
    Self = 3,
};

// 指令六：SkillCastType。
enum class SkillCastType : std::uint8_t {
    Instant = 1,
    CastTime = 2,
};

// 指令二十一：SkillResultCode（MalformedRequest 为协议层补充，阶段14 同名策略）。
enum class SkillResultCode : std::uint8_t {
    Success = 0,
    UnknownSkill = 1,
    InvalidTarget = 2,
    TargetDead = 3,
    CasterDead = 4,
    OutOfRange = 5,
    Cooldown = 6,
    NotEnoughMana = 7,
    AlreadyCasting = 8,
    NotInWorld = 9,
    DifferentMap = 10,
    DuplicateRequest = 11,
    Cancelled = 12,
    InternalError = 13,
    MalformedRequest = 14,
};

inline const char* SkillResultCodeName(std::uint8_t code) {
    switch (static_cast<SkillResultCode>(code)) {
        case SkillResultCode::Success: return "Success";
        case SkillResultCode::UnknownSkill: return "UnknownSkill";
        case SkillResultCode::InvalidTarget: return "InvalidTarget";
        case SkillResultCode::TargetDead: return "TargetDead";
        case SkillResultCode::CasterDead: return "CasterDead";
        case SkillResultCode::OutOfRange: return "OutOfRange";
        case SkillResultCode::Cooldown: return "Cooldown";
        case SkillResultCode::NotEnoughMana: return "NotEnoughMana";
        case SkillResultCode::AlreadyCasting: return "AlreadyCasting";
        case SkillResultCode::NotInWorld: return "NotInWorld";
        case SkillResultCode::DifferentMap: return "DifferentMap";
        case SkillResultCode::DuplicateRequest: return "DuplicateRequest";
        case SkillResultCode::Cancelled: return "Cancelled";
        case SkillResultCode::InternalError: return "InternalError";
        case SkillResultCode::MalformedRequest: return "MalformedRequest";
    }
    return "Unknown";
}

// 指令二十八：SkillCastCancelled 的 CancelReason。
enum class SkillCancelReason : std::uint8_t {
    Moved = 1,          // 指令十六：有效移动打断
    Dead = 2,           // 指令十七：死亡打断
    TargetInvalid = 3,  // 指令十八/四十二/四十三：完成时目标重验失败
    ServerCleanup = 4,  // 指令七十九：服务器 Stop 清理（不发网络包）
    Teleported = 5,     // 阶段20 指令七十二：NPC 传送打断
};

inline const char* SkillCancelReasonName(std::uint8_t reason) {
    switch (static_cast<SkillCancelReason>(reason)) {
        case SkillCancelReason::Moved: return "Moved";
        case SkillCancelReason::Dead: return "Dead";
        case SkillCancelReason::TargetInvalid: return "TargetInvalid";
        case SkillCancelReason::ServerCleanup: return "ServerCleanup";
        case SkillCancelReason::Teleported: return "Teleported";
    }
    return "Unknown";
}

// 指令二十九：SkillImpactEvent 单个命中目标。
struct SkillImpactTarget {
    std::uint8_t entityType = 0; // CombatEntityType（阶段15 只为 Monster）
    std::uint64_t entityId = 0;
    std::uint32_t damage = 0;
    std::uint32_t hpAfter = 0;
    std::uint32_t maxHp = 0;
    bool killed = false;
};

} // namespace legend::world
