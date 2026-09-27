#include "Client/Character/PlayerStatsComponent.h"

#include "Engine/Item/EquipmentComponent.h"
#include "Engine/Item/ItemDatabase.h"
#include "Engine/Progression/LevelSystem.h"

void PlayerStatsComponent::Initialize(const legend::combat::CombatStats& baseCombat) {
    m_baseStats = baseCombat; // base 副本（character.json combat）
}

void PlayerStatsComponent::ApplyLevelGrowth(const legend::progression::GrowthConfig& growth,
                                            int levelsGained) {
    if (levelsGained <= 0) {
        return;
    }
    // 阶段7 指令二十二：等级成长加到 Base Stats（不是 Final）
    m_baseStats.maxHp += growth.maxHpPerLevel * static_cast<float>(levelsGained);
    m_baseStats.attack += growth.attackPerLevel * static_cast<float>(levelsGained);
    m_baseStats.defense += growth.defensePerLevel * static_cast<float>(levelsGained);
}

void PlayerStatsComponent::RecalculateFinalStats(
    legend::combat::CombatStats& finalStats, const legend::item::EquipmentComponent& equipment,
    const legend::item::ItemDatabase& items) const {
    // final = base + equipment bonuses（阶段7 指令二十四公式；每次 Equip/Unequip/Swap/LevelUp 调用）
    const auto bonuses = equipment.CalculateBonuses(items);
    finalStats.maxHp = m_baseStats.maxHp + bonuses.maxHp;
    finalStats.attack = m_baseStats.attack + bonuses.attack;
    finalStats.defense = m_baseStats.defense + bonuses.defense;
    // attackRange/attackInterval/crit 等保持 base 值（本阶段不变）
    finalStats.attackRange = m_baseStats.attackRange;
    finalStats.attackInterval = m_baseStats.attackInterval;
}
