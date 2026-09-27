#include "Engine/Combat/CombatStats.h"

#include <algorithm>

namespace legend::combat {

bool CombatStats::IsValid() const {
    return maxHp > 0.0f && hp >= 0.0f && hp <= maxHp && attack >= 0.0f &&
           defense >= 0.0f && attackRange > 0.0f && attackInterval > 0.0f;
}

float CombatStats::TakeDamage(float amount) {
    if (!IsAlive() || amount <= 0.0f) {
        return 0.0f;
    }
    // 最低 1 点伤害（禁止 0/负伤害）
    const float finalDamage = std::max(1.0f, amount);
    hp = std::max(0.0f, hp - finalDamage);
    return finalDamage;
}

void CombatStats::Heal(float amount) {
    if (amount <= 0.0f || !IsAlive()) {
        return;
    }
    hp = std::min(maxHp, hp + amount);
}

void CombatStats::SetHp(float value) {
    hp = std::clamp(value, 0.0f, maxHp);
}

} // namespace legend::combat
