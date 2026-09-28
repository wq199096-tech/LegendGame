#pragma once

#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段14 指令二十四/三十六：DamageCalculator —— 纯函数伤害计算。
// rawDamage = attackPower - defense；damage = max(1, rawDamage)（指令二十四：
// 不随机、不暴击，保证测试稳定）。低攻击保底 1 点（指令九十七）。
// ---------------------------------------------------------------------------
std::uint32_t CalculateDamage(std::uint32_t attackPower, std::uint32_t defense);

} // namespace legend::world
