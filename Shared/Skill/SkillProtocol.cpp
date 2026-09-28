#include "Shared/Skill/SkillProtocol.h"

#include "Shared/Skill/SkillDefinition.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {
namespace {

// 与 WorldProtocol.cpp 相同模式：Encode 失败清空 out；Decode 必须完整消费
//（剩余字节 = malformed，指令八十六）。
template <typename Fn>
bool EncodePayload(std::vector<std::uint8_t>& out, Fn&& fill) {
    out.clear();
    legend::network::ByteWriter writer(out);
    if (!fill(writer)) {
        out.clear();
        return false;
    }
    return true;
}

template <typename Fn>
bool DecodePayload(const std::uint8_t* data, std::size_t size, std::string& error, Fn&& fill) {
    legend::network::ByteReader reader(data, size);
    fill(reader);
    if (!reader.IsValid()) {
        error = "malformed skill payload";
        return false;
    }
    if (reader.Remaining() != 0) {
        error = "malformed skill payload (trailing bytes)";
        return false;
    }
    return true;
}

} // namespace

bool EncodeSkillCastRequest(const SkillCastRequestPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteUInt32(p.skillId);
        w.WriteUInt8(p.targetType);
        w.WriteUInt64(p.targetEntityId);
        return true;
    });
}

bool DecodeSkillCastRequest(const std::uint8_t* data, std::size_t size,
                            SkillCastRequestPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.skillId = r.ReadUInt32();
        out.targetType = r.ReadUInt8();
        out.targetEntityId = r.ReadUInt64();
    });
}

bool EncodeSkillCastResponse(const SkillCastResponsePayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteUInt32(p.skillId);
        w.WriteBool(p.accepted);
        w.WriteUInt8(p.resultCode);
        w.WriteUInt32(p.currentMana);
        return w.WriteString(p.message);
    });
}

bool DecodeSkillCastResponse(const std::uint8_t* data, std::size_t size,
                             SkillCastResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.skillId = r.ReadUInt32();
        out.accepted = r.ReadBool();
        out.resultCode = r.ReadUInt8();
        out.currentMana = r.ReadUInt32();
        (void)(r.ReadString(out.message));
    });
}

bool EncodeSkillCastStarted(const SkillCastStartedPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.castId);
        w.WriteUInt64(p.casterCharacterId);
        w.WriteUInt32(p.skillId);
        w.WriteUInt8(p.targetType);
        w.WriteUInt64(p.targetEntityId);
        w.WriteUInt32(p.castTimeMs);
        w.WriteUInt64(p.serverTime);
        return true;
    });
}

bool DecodeSkillCastStarted(const std::uint8_t* data, std::size_t size,
                            SkillCastStartedPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.castId = r.ReadUInt64();
        out.casterCharacterId = r.ReadUInt64();
        out.skillId = r.ReadUInt32();
        out.targetType = r.ReadUInt8();
        out.targetEntityId = r.ReadUInt64();
        out.castTimeMs = r.ReadUInt32();
        out.serverTime = r.ReadUInt64();
    });
}

bool EncodeSkillCastCompleted(const SkillCastCompletedPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.castId);
        w.WriteUInt64(p.casterCharacterId);
        w.WriteUInt32(p.skillId);
        w.WriteUInt8(p.targetType);
        w.WriteUInt64(p.targetEntityId);
        w.WriteUInt64(p.serverTime);
        return true;
    });
}

bool DecodeSkillCastCompleted(const std::uint8_t* data, std::size_t size,
                              SkillCastCompletedPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.castId = r.ReadUInt64();
        out.casterCharacterId = r.ReadUInt64();
        out.skillId = r.ReadUInt32();
        out.targetType = r.ReadUInt8();
        out.targetEntityId = r.ReadUInt64();
        out.serverTime = r.ReadUInt64();
    });
}

bool EncodeSkillCastCancelled(const SkillCastCancelledPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.castId);
        w.WriteUInt64(p.casterCharacterId);
        w.WriteUInt32(p.skillId);
        w.WriteUInt8(p.reason);
        w.WriteUInt64(p.serverTime);
        return true;
    });
}

bool DecodeSkillCastCancelled(const std::uint8_t* data, std::size_t size,
                              SkillCastCancelledPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.castId = r.ReadUInt64();
        out.casterCharacterId = r.ReadUInt64();
        out.skillId = r.ReadUInt32();
        out.reason = r.ReadUInt8();
        out.serverTime = r.ReadUInt64();
    });
}

bool EncodeSkillImpactEvent(const SkillImpactEventPayload& p, std::vector<std::uint8_t>& out) {
    // 指令三十：Encode 超过 kSkillImpactMaxTargets 拒绝（Decode 同样拒绝）。
    if (p.targets.size() > kSkillImpactMaxTargets) {
        out.clear();
        return false;
    }
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.castId);
        w.WriteUInt32(p.skillId);
        w.WriteUInt64(p.casterCharacterId);
        w.WriteUInt64(p.serverTime);
        w.WriteUInt16(static_cast<std::uint16_t>(p.targets.size()));
        for (const auto& target : p.targets) {
            w.WriteUInt8(target.entityType);
            w.WriteUInt64(target.entityId);
            w.WriteUInt32(target.damage);
            w.WriteUInt32(target.hpAfter);
            w.WriteUInt32(target.maxHp);
            w.WriteBool(target.killed);
        }
        return true;
    });
}

bool DecodeSkillImpactEvent(const std::uint8_t* data, std::size_t size,
                            SkillImpactEventPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.castId = r.ReadUInt64();
        out.skillId = r.ReadUInt32();
        out.casterCharacterId = r.ReadUInt64();
        out.serverTime = r.ReadUInt64();
        const std::uint16_t count = r.ReadUInt16();
        if (count > kSkillImpactMaxTargets) {
            // 指令三十/一百三十八：count 超限 -> IsValid=false（Decode 失败）。
            r.Invalidate();
            return;
        }
        out.targets.reserve(count);
        for (std::uint16_t i = 0; i < count; ++i) {
            SkillImpactTarget target;
            target.entityType = r.ReadUInt8();
            target.entityId = r.ReadUInt64();
            target.damage = r.ReadUInt32();
            target.hpAfter = r.ReadUInt32();
            target.maxHp = r.ReadUInt32();
            target.killed = r.ReadBool();
            if (!r.IsValid()) {
                return; // 指令一百三十七：count 大于 payload -> 失败
            }
            out.targets.push_back(target);
        }
    });
}

bool EncodeManaSnapshot(const ManaSnapshotPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt32(p.currentMana);
        w.WriteUInt32(p.maxMana);
        w.WriteUInt64(p.serverTime);
        return true;
    });
}

bool DecodeManaSnapshot(const std::uint8_t* data, std::size_t size, ManaSnapshotPayload& out,
                        std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.currentMana = r.ReadUInt32();
        out.maxMana = r.ReadUInt32();
        out.serverTime = r.ReadUInt64();
    });
}

} // namespace legend::world
