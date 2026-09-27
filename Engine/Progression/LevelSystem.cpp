#include "Engine/Progression/LevelSystem.h"

namespace legend::progression {

std::vector<LevelUpEvent> LevelSystem::AddExperience(int& level, ExperienceValue& currentExp,
                                                     ExperienceValue& totalExp,
                                                     ExperienceValue amount) {
    std::vector<LevelUpEvent> events;
    if (amount <= 0) {
        return events;
    }
    if (level < 1) {
        level = 1;
    }
    // 64 位累加（阶段6.1）：3e9+ 的一次经验不会 signed overflow
    totalExp += amount;
    currentExp += amount;

    // 连续升级循环：一次大量经验必须能连升多级；64 位比较，无截断
    while (level < kMaxLevel) {
        const ExperienceValue required = RequiredExp(level);
        if (required <= 0 || currentExp < required) {
            break;
        }
        const int oldLevel = level;
        currentExp -= required; // required <= currentExp，保证 currentExp 不为负
        ++level;
        events.push_back({oldLevel, level});
    }
    if (level >= kMaxLevel) {
        currentExp = 0; // 满级封顶：currentExp 归 0（行为明确）
    }
    return events;
}

} // namespace legend::progression
