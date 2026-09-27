#pragma once

#include <vector>

#include "Engine/Progression/ExperienceTable.h"

namespace legend::progression {

// character.json "growth" 块：每级属性成长（缺省 20/5/2）
struct GrowthConfig {
    float maxHpPerLevel = 20.0f;
    float attackPerLevel = 5.0f;
    float defensePerLevel = 2.0f;
};

// 升级事件（oldLevel -> newLevel），UI/特效后续可订阅；当前 Logger 输出
struct LevelUpEvent {
    int oldLevel = 0;
    int newLevel = 0;
};

// 升级循环纯逻辑：加经验 -> 支持一次大量经验连续升级（不能只升 1 级）。
// MAX_LEVEL 封顶：满级后 currentExp 保持 0，继续击杀不再升级。
class LevelSystem {
public:
    // 推进 level/currentExp；totalExp 累计；返回本次产生的升级事件（0 到多次）
    static std::vector<LevelUpEvent> AddExperience(int& level, int& currentExp,
                                                   long long& totalExp, int amount);
};

} // namespace legend::progression
