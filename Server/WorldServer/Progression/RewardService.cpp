#include "Server/WorldServer/Progression/RewardService.h"

namespace legend::world {

// 指令十三：击杀奖励事件（只发本人）。
RewardGrantedPayload BuildRewardEvent(std::uint64_t characterId,
                                      std::uint64_t sourceMonsterEntityId, std::uint32_t expGain,
                                      std::uint32_t goldGain, std::int64_t newExperience,
                                      std::int64_t newGold, std::uint32_t level,
                                      std::uint64_t serverTime) {
    RewardGrantedPayload payload;
    payload.characterId = characterId;
    payload.sourceMonsterEntityId = sourceMonsterEntityId;
    payload.expGranted = expGain;
    payload.goldGranted = goldGain;
    payload.newExperience = newExperience;
    payload.newGold = newGold;
    payload.level = level;
    payload.serverTime = serverTime;
    return payload;
}

// 指令十四：升级事件（属性成长按新等级计算，指令十）。
LevelUpEventPayload BuildLevelUpEvent(std::uint64_t characterId, std::uint32_t oldLevel,
                                      std::uint32_t newLevel, std::int64_t currentExp,
                                      std::uint64_t serverTime) {
    LevelUpEventPayload payload;
    payload.characterId = characterId;
    payload.oldLevel = oldLevel;
    payload.newLevel = newLevel;
    payload.currentExp = currentExp;
    payload.nextLevelExp =
        newLevel >= kProgressionMaxLevel ? 0 : ExpToNextLevel(newLevel); // 满级为 0
    const auto stats = CalculateLevelUp(newLevel);
    payload.newMaxHp = stats.maxHp;
    payload.newAttackPower = stats.attackPower;
    payload.newDefense = stats.defense;
    payload.serverTime = serverTime;
    return payload;
}

// 指令十五：进世界下发 + 每 30s 本人纠偏。
ProgressionSnapshotPayload BuildProgressionSnapshot(std::uint64_t characterId,
                                                    std::uint32_t level, std::int64_t experience,
                                                    std::int64_t gold, std::uint64_t serverTime) {
    ProgressionSnapshotPayload payload;
    payload.characterId = characterId;
    payload.level = level;
    payload.experience = experience;
    payload.expToNext =
        level >= kProgressionMaxLevel ? 0 : ExpToNextLevel(level) - experience; // 满级为 0
    payload.gold = gold;
    payload.serverTime = serverTime;
    return payload;
}

} // namespace legend::world
