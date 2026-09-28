#include "Server/WorldServer/Combat/DamageCalculator.h"

namespace legend::world {

std::uint32_t CalculateDamage(std::uint32_t attackPower, std::uint32_t defense) {
    // 指令二十四：damage = max(1, attackPower - defense)。
    if (attackPower <= defense) {
        return 1u; // 保底 1 点伤害
    }
    return attackPower - defense;
}

} // namespace legend::world
