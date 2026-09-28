#pragma once

#include "Shared/Progression/ProgressionTypes.h"

#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段17 指令八/九：ProgressionService —— 成长规则纯逻辑（无 IO/无网络）。
// WorldServer 负责编排（击杀归属解析、DB 持久化、广播）；本服务只负责：
//   AddExperience / AddGold / CalculateLevelUp / ApplyLevelProgression。
// 经验公式 ExpToNextLevel(level) = 100 * level（指令四）；maxLevel = 100（指令五）。
// 升级属性成长：MaxHp +10 / Attack +2 / Defense +1（指令十；Mana 保持 100）。
// ---------------------------------------------------------------------------

// 指令八：AddExperience —— 返回应用 expGain 后的成长结果（支持一次奖励跨多级，
// 满级后 EXP 不再累计升级、不溢出）。
LevelProgressResult AddExperience(std::uint32_t level, std::int64_t exp, std::int64_t expGain);

// 指令八：AddGold —— 金币只累加，不参与升级；不允许负数。
std::int64_t AddGold(std::int64_t gold, std::int64_t goldGain);

// 指令八/十：CalculateLevelUp —— 结算后的等级基础属性（Derived Stats 的 Base 部分）。
struct LevelBaseStats {
    std::uint32_t maxHp = 0;
    std::uint32_t attackPower = 0;
    std::uint32_t defense = 0;
};
LevelBaseStats CalculateLevelUp(std::uint32_t level);

} // namespace legend::world
