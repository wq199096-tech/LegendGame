#pragma once

#include <cstdint>
#include <string>

#include "Engine/Entity/EntityId.h"

namespace legend::combat {

// 伤害事件：统一由 CombatResolver/CombatSystem 产生。
// 预留 skillId / critical / damageType（阶段5不使用）。
struct DamageEvent {
    legend::entity::EntityId sourceId = legend::entity::kInvalidEntityId;
    legend::entity::EntityId targetId = legend::entity::kInvalidEntityId;
    float rawDamage = 0.0f;
    float finalDamage = 0.0f;
    uint64_t sequence = 0; // 全局递增序号
    // 阶段8：伤害来源技能 id（普通攻击为空；技能 = skillId），不破坏旧代码
    std::string abilityId;
};

} // namespace legend::combat
