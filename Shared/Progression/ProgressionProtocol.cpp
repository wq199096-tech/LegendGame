#include "Shared/Progression/ProgressionProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {

namespace {
using legend::network::ByteReader;
using legend::network::ByteWriter;
// ByteWriter/ByteReader 无 Int64 接口——exp/gold 非负（<2^63），用 UInt64 位模式
// 双向传输无损（static_cast 语义）。
inline std::uint64_t ToBits(std::int64_t v) { return static_cast<std::uint64_t>(v); }
inline std::int64_t FromBits(std::uint64_t v) { return static_cast<std::int64_t>(v); }
} // namespace

// RewardGranted(280)（指令十三）。
bool EncodeRewardGranted(const RewardGrantedPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.characterId);
    w.WriteUInt64(p.sourceMonsterEntityId);
    w.WriteUInt32(p.expGranted);
    w.WriteUInt32(p.goldGranted);
    w.WriteUInt64(ToBits(p.newExperience));
    w.WriteUInt64(ToBits(p.newGold));
    w.WriteUInt32(p.level);
    w.WriteUInt64(p.serverTime);
    return true; // 定长字段无失败路径（String 才可能失败）
}

bool DecodeRewardGranted(const std::uint8_t* data, std::size_t size,
                         RewardGrantedPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.characterId = r.ReadUInt64();
    out.sourceMonsterEntityId = r.ReadUInt64();
    out.expGranted = r.ReadUInt32();
    out.goldGranted = r.ReadUInt32();
    out.newExperience = FromBits(r.ReadUInt64());
    out.newGold = FromBits(r.ReadUInt64());
    out.level = r.ReadUInt32();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed RewardGranted payload";
        return false;
    }
    return true;
}

// LevelUpEvent(281)（指令十四）。
bool EncodeLevelUpEvent(const LevelUpEventPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.characterId);
    w.WriteUInt32(p.oldLevel);
    w.WriteUInt32(p.newLevel);
    w.WriteUInt64(ToBits(p.currentExp));
    w.WriteUInt64(ToBits(p.nextLevelExp));
    w.WriteUInt32(p.newMaxHp);
    w.WriteUInt32(p.newAttackPower);
    w.WriteUInt32(p.newDefense);
    w.WriteUInt64(p.serverTime);
    return true; // 定长字段无失败路径（String 才可能失败）
}

bool DecodeLevelUpEvent(const std::uint8_t* data, std::size_t size,
                        LevelUpEventPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.characterId = r.ReadUInt64();
    out.oldLevel = r.ReadUInt32();
    out.newLevel = r.ReadUInt32();
    out.currentExp = FromBits(r.ReadUInt64());
    out.nextLevelExp = FromBits(r.ReadUInt64());
    out.newMaxHp = r.ReadUInt32();
    out.newAttackPower = r.ReadUInt32();
    out.newDefense = r.ReadUInt32();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed LevelUpEvent payload";
        return false;
    }
    return true;
}

// ProgressionSnapshot(282)（指令十五）。
bool EncodeProgressionSnapshot(const ProgressionSnapshotPayload& p,
                               std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.characterId);
    w.WriteUInt32(p.level);
    w.WriteUInt64(ToBits(p.experience));
    w.WriteUInt64(ToBits(p.expToNext));
    w.WriteUInt64(ToBits(p.gold));
    w.WriteUInt64(p.serverTime);
    return true; // 定长字段无失败路径（String 才可能失败）
}

bool DecodeProgressionSnapshot(const std::uint8_t* data, std::size_t size,
                               ProgressionSnapshotPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.characterId = r.ReadUInt64();
    out.level = r.ReadUInt32();
    out.experience = FromBits(r.ReadUInt64());
    out.expToNext = FromBits(r.ReadUInt64());
    out.gold = FromBits(r.ReadUInt64());
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed ProgressionSnapshot payload";
        return false;
    }
    return true;
}

} // namespace legend::world

