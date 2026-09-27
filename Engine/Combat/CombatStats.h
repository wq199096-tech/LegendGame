#pragma once

namespace legend::combat {

// 战斗属性组件：数据驱动（character.json / monster.json 的 "combat" 块）。
// 不散落进 Character 字段；后续 crit/闪避/攻速等在此扩展（本阶段不使用）。
struct CombatStats {
    float maxHp = 100.0f;
    float hp = 100.0f;
    float attack = 10.0f;
    float defense = 0.0f;
    float attackRange = 80.0f;
    float attackInterval = 1.0f;

    // 预留字段（阶段5不参与计算）
    float critChance = 0.0f;
    float critDamage = 0.0f;
    float moveSpeedBonus = 0.0f;
    float attackSpeed = 1.0f;
    float accuracy = 0.0f;
    float evasion = 0.0f;

    // 规则校验：maxHp>0，0<=hp<=maxHp，attack/defense>=0，attackRange>0，attackInterval>0
    bool IsValid() const;

    bool IsAlive() const { return hp > 0.0f; }

    // 受击：伤害下限 1（禁止负伤害/0伤害），HP 下限 0；返回实际伤害（已死返回 0）
    float TakeDamage(float amount);
    // 治疗：不超过 maxHp
    void Heal(float amount);
    // 直接设置 HP：clamp 到 [0, maxHp]
    void SetHp(float value);
    float GetHpPercent() const { return maxHp > 0.0f ? hp / maxHp : 0.0f; }
};

} // namespace legend::combat
