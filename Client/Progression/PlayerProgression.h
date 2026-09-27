#pragma once

#include <vector>

#include "Engine/Combat/CombatStats.h"
#include "Engine/Progression/LevelSystem.h"

namespace legend::animation {
struct CharacterDefinition;
}

namespace legend::progression {

// 玩家成长组件（PlayerCharacter 组合持有，不做 LevelPlayer 继承）：
// level 从 1 开始；AddExperience 支持连续升级；满级（50）封顶 currentExp 归 0。
// 阶段7：growth 属性应用移到 PlayerStatsComponent（Base Stats 架构）——本组件只管
// level/exp 推进与升级事件，不直接修改 CombatStats（杜绝 Base/Equipment/Final 漂移）。
// 阶段8.2：Snapshot API——Skill 测试隔离用（Restore 校验+清 pending 升级事件）。
struct PlayerProgressionSnapshot {
    int level = 1;
    ExperienceValue currentExp = 0;
    ExperienceValue totalExp = 0;
};

class PlayerProgression {
public:
    PlayerProgression() = default;

    // 初始化成长配置（来自 character.json growth 块）
    void Initialize(const legend::animation::CharacterDefinition& definition);

    // 加经验：内部连续升级并返回升级事件（不修改 CombatStats——growth 由调用方通过
    // PlayerStatsComponent::ApplyLevelGrowth 应用到 base stats 后 RecalculateFinalStats）
    std::vector<LevelUpEvent> AddExperience(ExperienceValue amount);

    // 阶段8.2：快照（SkillWorldSnapshot 隔离验收用）
    PlayerProgressionSnapshot CreateSnapshot() const;
    // 恢复：校验 level∈[1,kMaxLevel]、currentExp/totalExp>=0；清空 pendingLevelUps
    //（不留测试升级事件，指令三/十一）；非法快照返回 false 且不修改任何状态
    bool RestoreSnapshot(const PlayerProgressionSnapshot& snapshot);

    int GetLevel() const { return m_level; }
    ExperienceValue GetCurrentExp() const { return m_currentExp; }
    ExperienceValue GetTotalExp() const { return m_totalExp; }
    ExperienceValue GetRequiredExp() const { return RequiredExp(m_level); } // 满级返回 0
    float GetExpPercent() const;                                            // 满级返回 1
    bool CanLevelUp() const {
        return m_level < kMaxLevel && GetRequiredExp() > 0 && m_currentExp >= GetRequiredExp();
    }

    const std::vector<LevelUpEvent>& GetPendingLevelUps() const { return m_pendingLevelUps; }
    void ClearPendingLevelUps() { m_pendingLevelUps.clear(); }
    const GrowthConfig& GetGrowth() const { return m_growth; }

private:
    int m_level = 1;
    ExperienceValue m_currentExp = 0;
    ExperienceValue m_totalExp = 0;
    GrowthConfig m_growth;
    std::vector<LevelUpEvent> m_pendingLevelUps; // 未消费的升级事件（升级时 push）
};

} // namespace legend::progression
