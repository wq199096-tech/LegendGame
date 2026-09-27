#pragma once

#include "Engine/Combat/CombatStats.h"

namespace legend::progression {
struct GrowthConfig;
}

namespace legend::item {
class EquipmentComponent;
class ItemDatabase;
}

// 玩家属性组件（阶段7 Base/Equipment/Final 架构，全局命名空间与 PlayerCharacter 一致）：
// - base stats：character.json combat 的副本（阶段6 的成长现在加到 base，不再直接改 final）
// - ApplyLevelGrowth：升级成长加到 base（每级 maxHp/attack/defense + growth）
// - RecalculateFinalStats：final = base + equipment bonuses；
//   attackRange/attackInterval/crit 等本阶段保持 base 值
// 不让 EquipmentComponent 直接修改 Character 内部数据（指令二十七）。
class PlayerStatsComponent {
public:
    // base = character.json combat 副本（初始化时保存一份）
    void Initialize(const legend::combat::CombatStats& baseCombat);

    const legend::combat::CombatStats& GetBaseStats() const { return m_baseStats; }

    // 阶段8.2：直接恢复 Base 快照（Skill 测试隔离用——禁止"升几级再减回去"）
    void RestoreBaseStats(const legend::combat::CombatStats& stats) { m_baseStats = stats; }

    // 等级成长加到 base stats（levelsGained = 本次升级级数；每级 +growth 三项）
    void ApplyLevelGrowth(const legend::progression::GrowthConfig& growth, int levelsGained);

    // final = base + equipment 加成（maxHp/attack/defense 三项；
    // attackRange/attackInterval 等保持 base 值——阶段7 不动）。
    // HP clamp 由调用方处理（装备/升级的 HP 语义不同）。
    void RecalculateFinalStats(legend::combat::CombatStats& finalStats,
                               const legend::item::EquipmentComponent& equipment,
                               const legend::item::ItemDatabase& items) const;

private:
    legend::combat::CombatStats m_baseStats; // base 副本（combat 块）
};
