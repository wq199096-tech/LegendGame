#pragma once

#include <algorithm>

namespace legend::skill {

// 玩家法力资源（阶段8指令十二）：独立组件，不塞进 CombatStats。
// 初始 Mana = MaxMana；升级暂不加 MP 成长（指令十四）；死亡 Respawn 时 Fill。
class SkillResource {
public:
    void Initialize(float maxMana) {
        m_maxMana = maxMana > 0.0f ? maxMana : 0.0f;
        m_mana = m_maxMana; // 初始满蓝
    }

    float GetMana() const { return m_mana; }
    float GetMaxMana() const { return m_maxMana; }
    float GetManaPercent() const { return m_maxMana > 0.0f ? m_mana / m_maxMana : 0.0f; }

    bool CanSpend(float amount) const { return amount >= 0.0f && m_mana >= amount; }

    // 扣蓝：不足时失败且数值不变
    bool Spend(float amount) {
        if (!CanSpend(amount)) {
            return false;
        }
        m_mana -= amount;
        return true;
    }

    // 回蓝：clamp 到 maxMana
    void Restore(float amount) {
        if (amount <= 0.0f) {
            return;
        }
        m_mana = std::min(m_mana + amount, m_maxMana);
    }

    void SetMana(float value) { m_mana = std::clamp(value, 0.0f, m_maxMana); }
    void FillMana() { m_mana = m_maxMana; }

private:
    float m_maxMana = 0.0f;
    float m_mana = 0.0f;
};

} // namespace legend::skill
