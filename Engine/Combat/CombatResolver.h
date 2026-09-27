#pragma once

namespace legend::combat {

// 伤害公式集中地：Player / Monster 统一调用，禁止各写一套。
// 阶段5公式：RawDamage = Attack - Defense；FinalDamage = max(1, RawDamage)。
// 不做暴击/闪避/元素/抗性/穿透。
class CombatResolver {
public:
    // Attack - Defense（可为负，调用方一般用 ComputeFinalDamage）
    static float ComputeRawDamage(float attack, float defense);
    // max(1, Attack - Defense)：稳定下限 1
    static float ComputeFinalDamage(float attack, float defense);
};

} // namespace legend::combat
