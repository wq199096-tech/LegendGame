#pragma once

#include "Server/WorldServer/Progression/ProgressionService.h"
#include "Shared/Progression/ProgressionProtocol.h"
#include "Shared/Progression/ProgressionTypes.h"

#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段17 指令八/九：RewardService —— 奖励事件构建纯逻辑（无 IO/无网络）。
// WorldServer 只负责编排（击杀归属解析、PlayerSession 查询、DbWorker 持久化、
// 广播范围计算）；本服务只负责 BuildRewardEvent / BuildLevelUpEvent /
// BuildProgressionSnapshot 三个事件构建函数（服务器权威数值，Client 不能决定）。
// ---------------------------------------------------------------------------

// 指令十三：BuildRewardEvent —— 击杀奖励事件（只发给本人，指令十六）。
RewardGrantedPayload BuildRewardEvent(std::uint64_t characterId,
                                      std::uint64_t sourceMonsterEntityId, std::uint32_t expGain,
                                      std::uint32_t goldGain, std::int64_t newExperience,
                                      std::int64_t newGold, std::uint32_t level,
                                      std::uint64_t serverTime);

// 指令十四：BuildLevelUpEvent —— 升级事件（本人 + 能看到该 Player 的附近玩家）。
// 属性成长按新等级计算（MaxHp +10 / Attack +2 / Defense +1 每级，指令十）。
LevelUpEventPayload BuildLevelUpEvent(std::uint64_t characterId, std::uint32_t oldLevel,
                                      std::uint32_t newLevel, std::int64_t currentExp,
                                      std::uint64_t serverTime);

// 指令十五：BuildProgressionSnapshot —— 进入世界下发 + 每 30s 本人纠偏。
ProgressionSnapshotPayload BuildProgressionSnapshot(std::uint64_t characterId,
                                                    std::uint32_t level, std::int64_t experience,
                                                    std::int64_t gold, std::uint64_t serverTime);

} // namespace legend::world
