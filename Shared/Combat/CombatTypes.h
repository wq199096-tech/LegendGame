#pragma once

#include <cstddef>
#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段14：服务器权威战斗共享类型（指令八~十一/三十一）。
// 100% WorldServer 权威（指令一）：Client 只发"我想攻击谁"，伤害/命中/死亡
// 全部由服务器决定（禁止 Client 传攻击坐标/hitbox/damage，指令三）。
// ---------------------------------------------------------------------------

// 指令八：CombatEntityType。
enum class CombatEntityType : std::uint8_t {
    Player = 1,
    Monster = 2,
};

// 指令九/三十一：CombatResultCode。
enum class CombatResultCode : std::uint8_t {
    Success = 0,
    InvalidTarget = 1,
    TargetDead = 2,
    AttackerDead = 3,
    OutOfRange = 4,
    Cooldown = 5,
    NotInWorld = 6,
    DifferentMap = 7,
    MalformedRequest = 8,
    InternalError = 9,
    DuplicateRequest = 10,
};

inline const char* CombatResultCodeName(std::uint8_t code) {
    switch (static_cast<CombatResultCode>(code)) {
        case CombatResultCode::Success: return "Success";
        case CombatResultCode::InvalidTarget: return "InvalidTarget";
        case CombatResultCode::TargetDead: return "TargetDead";
        case CombatResultCode::AttackerDead: return "AttackerDead";
        case CombatResultCode::OutOfRange: return "OutOfRange";
        case CombatResultCode::Cooldown: return "Cooldown";
        case CombatResultCode::NotInWorld: return "NotInWorld";
        case CombatResultCode::DifferentMap: return "DifferentMap";
        case CombatResultCode::MalformedRequest: return "MalformedRequest";
        case CombatResultCode::InternalError: return "InternalError";
        case CombatResultCode::DuplicateRequest: return "DuplicateRequest";
    }
    return "Unknown";
}

// 指令四：玩家固定基础战斗属性（阶段14 全体一致，不做装备/成长加成）。
inline constexpr std::uint32_t kPlayerMaxHp = 100;
inline constexpr std::uint32_t kPlayerAttackPower = 20;
inline constexpr std::uint32_t kPlayerDefense = 5;
inline constexpr float kPlayerAttackRange = 100.0f;
inline constexpr float kPlayerAttackCooldownSeconds = 0.8f;

// 指令五：Training Slime 战斗属性（写在 MonsterDefinition，见 MonsterDefinition.h）。

// 指令五十一/五十四/七十七：怪物死亡 3 秒后清理（统一 AI tick 检查，不建 per-monster timer）。
inline constexpr float kMonsterDeathCleanupSeconds = 3.0f;

// 指令六十八：Health Snapshot 纠偏周期（1s，单条不发 batch）。
inline constexpr int kHealthSnapshotIntervalMs = 1000;

// 指令三十：每 PlayerSession 最近 64 个攻击 requestId（防网络重放重复扣血）。
inline constexpr std::size_t kAttackRequestHistorySize = 64;

} // namespace legend::world
