#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段17 指令十三~十五：成长/奖励协议 payload（服务器权威，Client 只接收）。
// MessageId：RewardGranted=280 / LevelUpEvent=281 / ProgressionSnapshot=282。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0。
// 广播范围（指令十六）：RewardGranted 只给本人；LevelUpEvent 给本人+能看到该
// Player 的附近玩家；ProgressionSnapshot 进世界下发 + 每 30s 本人纠偏。
// ---------------------------------------------------------------------------

// RewardGranted(280)（指令十三）。
struct RewardGrantedPayload {
    std::uint64_t characterId = 0;
    std::uint64_t sourceMonsterEntityId = 0;
    std::uint32_t expGranted = 0;
    std::uint32_t goldGranted = 0;
    std::int64_t newExperience = 0;
    std::int64_t newGold = 0;
    std::uint32_t level = 1;
    std::uint64_t serverTime = 0;
};

// LevelUpEvent(281)（指令十四）。
struct LevelUpEventPayload {
    std::uint64_t characterId = 0;
    std::uint32_t oldLevel = 1;
    std::uint32_t newLevel = 1;
    std::int64_t currentExp = 0;
    std::int64_t nextLevelExp = 0; // 满级为 0
    std::uint32_t newMaxHp = 0;
    std::uint32_t newAttackPower = 0;
    std::uint32_t newDefense = 0;
    std::uint64_t serverTime = 0;
};

// ProgressionSnapshot(282)（指令十五）：进世界下发 + 每 30s 本人纠偏。
struct ProgressionSnapshotPayload {
    std::uint64_t characterId = 0;
    std::uint32_t level = 1;
    std::int64_t experience = 0;
    std::int64_t expToNext = 0; // 满级为 0
    std::int64_t gold = 0;
    std::uint64_t serverTime = 0;
};

bool EncodeRewardGranted(const RewardGrantedPayload& p, std::vector<std::uint8_t>& out);
bool DecodeRewardGranted(const std::uint8_t* data, std::size_t size,
                         RewardGrantedPayload& out, std::string& error);
bool EncodeLevelUpEvent(const LevelUpEventPayload& p, std::vector<std::uint8_t>& out);
bool DecodeLevelUpEvent(const std::uint8_t* data, std::size_t size,
                        LevelUpEventPayload& out, std::string& error);
bool EncodeProgressionSnapshot(const ProgressionSnapshotPayload& p,
                               std::vector<std::uint8_t>& out);
bool DecodeProgressionSnapshot(const std::uint8_t* data, std::size_t size,
                               ProgressionSnapshotPayload& out, std::string& error);

} // namespace legend::world
