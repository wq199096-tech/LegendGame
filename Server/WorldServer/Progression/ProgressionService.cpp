#include "Server/WorldServer/Progression/ProgressionService.h"

#include <algorithm>

namespace legend::world {

// 指令四/五/八：经验结算（跨多级 while；满级 EXP 不累计、归零余量不溢出）。
LevelProgressResult AddExperience(std::uint32_t level, std::int64_t exp, std::int64_t expGain) {
    return ApplyLevelProgression(level, exp, expGain);
}

// 指令八：金币累加（clamp 0，永不透支）。
std::int64_t AddGold(std::int64_t gold, std::int64_t goldGain) {
    return std::max<std::int64_t>(0, gold + goldGain);
}

// 指令十：等级 -> 基础属性（level 1 = 100/20/5，每级 +10/+2/+1）。
LevelBaseStats CalculateLevelUp(std::uint32_t level) {
    LevelBaseStats stats;
    stats.maxHp = BaseMaxHpForLevel(level);
    stats.attackPower = BaseAttackPowerForLevel(level);
    stats.defense = BaseDefenseForLevel(level);
    return stats;
}

} // namespace legend::world
