#include "Engine/Progression/LevelSystem.h"

namespace legend::progression {

std::vector<LevelUpEvent> LevelSystem::AddExperience(int& level, int& currentExp,
                                                     long long& totalExp, int amount) {
    std::vector<LevelUpEvent> events;
    if (amount <= 0) {
        return events;
    }
    if (level < 1) {
        level = 1;
    }
    totalExp += amount;
    currentExp += amount;

    // 连续升级循环：一次大量经验必须能连升多级
    while (level < kMaxLevel) {
        const int required = RequiredExp(level);
        if (required <= 0 || currentExp < required) {
            break;
        }
        const int oldLevel = level;
        currentExp -= required;
        ++level;
        events.push_back({oldLevel, level});
    }
    if (level >= kMaxLevel) {
        currentExp = 0; // 满级封顶：currentExp 归 0（行为明确）
    }
    return events;
}

} // namespace legend::progression
