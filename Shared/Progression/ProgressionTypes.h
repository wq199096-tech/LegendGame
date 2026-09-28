#pragma once

#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段17：服务器权威成长核心常量与纯函数（Progression, Reward & Respawn V0.17）。
// EXP/Gold/Level/LevelUp/怪物奖励/怪物 Respawn 全部 WorldServer 权威（指令一）；
// Client 不能发送"我要多少经验/金币/升级/怪物应该复活/奖励倍率"。
// ---------------------------------------------------------------------------

// 指令四：经验公式 —— ExpToNextLevel(level) = 100 * level。
inline constexpr std::int64_t ExpToNextLevel(std::uint32_t level) {
    return 100 * static_cast<std::int64_t>(level);
}

// 指令五：等级上限（满级后 EXP 不再导致 LevelUp，不溢出）。
inline constexpr std::uint32_t kProgressionMaxLevel = 100;

// 指令六：Training Slime 奖励（加入 MonsterDefinition 的默认值）。
inline constexpr std::uint32_t kTrainingSlimeRewardExp = 25;
inline constexpr std::uint32_t kTrainingSlimeRewardGold = 3;

// 指令十：每提升 1 级属性成长（Mana 不升级保持 100，指令十）。
inline constexpr std::uint32_t kLevelGrowthMaxHp = 10;
inline constexpr std::uint32_t kLevelGrowthAttackPower = 2;
inline constexpr std::uint32_t kLevelGrowthDefense = 1;

// 阶段14/16 玩家基础属性（level 1 时）。
inline constexpr std::uint32_t kPlayerBaseMaxHp = 100;
inline constexpr std::uint32_t kPlayerBaseAttackPower = 20;
inline constexpr std::uint32_t kPlayerBaseDefense = 5;

// 指令十：按等级计算基础属性（level 1 = 基础值；每级线性成长）。
inline std::uint32_t BaseMaxHpForLevel(std::uint32_t level) {
    return kPlayerBaseMaxHp + kLevelGrowthMaxHp * (level - 1);
}
inline std::uint32_t BaseAttackPowerForLevel(std::uint32_t level) {
    return kPlayerBaseAttackPower + kLevelGrowthAttackPower * (level - 1);
}
inline std::uint32_t BaseDefenseForLevel(std::uint32_t level) {
    return kPlayerBaseDefense + kLevelGrowthDefense * (level - 1);
}

// 指令四/五：ApplyLevelProgression —— 把 expGain 应用到 (level, exp) 上。
// 支持一次奖励跨多级（while 循环）；满级（100）后 exp 不再累计升级、不溢出。
struct LevelProgressResult {
    std::uint32_t level = 1;      // 新等级
    std::int64_t exp = 0;         // 新当前经验（升级后余量）
    std::uint32_t levelsGained = 0;
    bool levelUp = false;
    bool atMaxLevel = false;      // 结算时已满级（EXP 不再累计）
};

inline LevelProgressResult ApplyLevelProgression(std::uint32_t level, std::int64_t exp,
                                                 std::int64_t expGain) {
    LevelProgressResult result;
    result.level = level;
    result.exp = exp;
    if (level >= kProgressionMaxLevel) {
        result.atMaxLevel = true;
        return result; // 满级：EXP 不累计（指令五：不溢出）
    }
    result.exp += expGain;
    while (result.level < kProgressionMaxLevel && result.exp >= ExpToNextLevel(result.level)) {
        result.exp -= ExpToNextLevel(result.level);
        ++result.level;
        ++result.levelsGained;
        result.levelUp = true;
    }
    if (result.level >= kProgressionMaxLevel) {
        result.level = kProgressionMaxLevel;
        result.exp = 0; // 满级归零余量（指令五：不溢出）
    }
    return result;
}

} // namespace legend::world
