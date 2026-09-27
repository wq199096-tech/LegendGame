#include "Engine/Combat/CombatResolver.h"

#include <algorithm>

namespace legend::combat {

float CombatResolver::ComputeRawDamage(float attack, float defense) {
    return attack - defense;
}

float CombatResolver::ComputeFinalDamage(float attack, float defense) {
    return std::max(1.0f, ComputeRawDamage(attack, defense));
}

} // namespace legend::combat
