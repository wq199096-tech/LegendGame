#pragma once

#include "Shared/Skill/SkillTypes.h"

#include <cstdint>
#include <string>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段15 指令七/三十四：SkillDefinition —— 技能静态配置。
// 阶段15 硬编码三个测试技能（SkillRegistry），不上 JSON、不进数据库（指令九十六：
// Definition 不能由 Client 上传；Client 只传 skillId）。
// ---------------------------------------------------------------------------

struct SkillDefinition {
    SkillId skillId = 0;
    std::string name;
    SkillCastType castType = SkillCastType::Instant;
    SkillTargetType targetType = SkillTargetType::None;
    float cooldownSeconds = 0.0f;
    float castTimeSeconds = 0.0f;
    std::uint32_t manaCost = 0;
    float range = 0.0f;        // 单体目标距离上限 / Self 技能为 0
    std::uint32_t baseDamage = 0;
    float aoeRadius = 0.0f;    // 0 = 非 AOE
    std::uint32_t maxTargets = 1; // AOE 上限（指令三十/八十五：最多 16）
};

// 指令八：Quick Strike —— Instant 单体。
inline const SkillDefinition kQuickStrikeDefinition{
    kSkillIdQuickStrike, "Quick Strike", SkillCastType::Instant, SkillTargetType::Monster,
    1.5f, 0.0f, 10u, 120.0f, 30u, 0.0f, 1u,
};

// 指令九：Fire Bolt —— CastTime 单体（1s 施法；阶段15 无飞行 Projectile，
// cast 完成时服务器直接结算伤害）。
inline const SkillDefinition kFireBoltDefinition{
    kSkillIdFireBolt, "Fire Bolt", SkillCastType::CastTime, SkillTargetType::Monster,
    3.0f, 1.0f, 20u, 500.0f, 40u, 0.0f, 1u,
};

// 指令十：Whirlwind —— Instant 自体 AOE（只伤害附近 alive Monster，不伤玩家）。
inline const SkillDefinition kWhirlwindDefinition{
    kSkillIdWhirlwind, "Whirlwind", SkillCastType::Instant, SkillTargetType::Self,
    5.0f, 0.0f, 25u, 0.0f, 25u, 160.0f, 16u,
};

// 指令三十/八十五：SkillImpact 最大目标数（Decode 超过拒绝）。
inline constexpr std::size_t kSkillImpactMaxTargets = 16;

// 指令十一：玩家 Mana（阶段15 固定 100/100，不持久化——重启/重进恢复满）。
inline constexpr std::uint32_t kPlayerMaxMana = 100;

} // namespace legend::world
