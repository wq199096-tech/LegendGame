#pragma once

#include <cstdint>

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
};

} // namespace legend::combat
