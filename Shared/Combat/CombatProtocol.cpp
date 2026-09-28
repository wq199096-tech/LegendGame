#include "Shared/Combat/CombatProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {
namespace {

// 与 WorldProtocol / MonsterProtocol 相同模式：Encode 失败清空 out；
// Decode 必须完整消费（指令八十二）。
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
        error = "malformed combat payload";
        return false;
    }
    if (reader.Remaining() != 0) {
        error = "malformed combat payload (trailing bytes)";
        return false;
    }
    return true;
}

} // namespace

bool EncodePlayerAttackRequest(const PlayerAttackRequestPayload& p,
                               std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteUInt8(p.targetEntityType);
        w.WriteUInt64(p.targetEntityId);
        return true;
    });
}

bool DecodePlayerAttackRequest(const std::uint8_t* data, std::size_t size,
                               PlayerAttackRequestPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.targetEntityType = r.ReadUInt8();
        out.targetEntityId = r.ReadUInt64();
    });
}

bool EncodePlayerAttackResponse(const PlayerAttackResponsePayload& p,
                                std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteBool(p.success);
        w.WriteUInt8(p.resultCode);
        w.WriteUInt64(p.targetEntityId);
        return w.WriteString(p.message);
    });
}

bool DecodePlayerAttackResponse(const std::uint8_t* data, std::size_t size,
                                PlayerAttackResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.success = r.ReadBool();
        out.resultCode = r.ReadUInt8();
        out.targetEntityId = r.ReadUInt64();
        (void)(r.ReadString(out.message));
    });
}

bool EncodeCombatEvent(const CombatEventPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.eventId);
        w.WriteUInt8(p.attackerType);
        w.WriteUInt64(p.attackerId);
        w.WriteUInt8(p.targetType);
        w.WriteUInt64(p.targetId);
        w.WriteUInt32(p.damage);
        w.WriteUInt32(p.targetHpAfter);
        w.WriteUInt32(p.targetMaxHp);
        w.WriteBool(p.killed);
        w.WriteUInt64(p.serverTime);
        // 阶段15 指令三十二：伤害来源（BasicAttack/Skill + skillId）。
        w.WriteUInt8(p.sourceType);
        w.WriteUInt64(p.sourceId);
        return true;
    });
}

bool DecodeCombatEvent(const std::uint8_t* data, std::size_t size, CombatEventPayload& out,
                       std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.eventId = r.ReadUInt64();
        out.attackerType = r.ReadUInt8();
        out.attackerId = r.ReadUInt64();
        out.targetType = r.ReadUInt8();
        out.targetId = r.ReadUInt64();
        out.damage = r.ReadUInt32();
        out.targetHpAfter = r.ReadUInt32();
        out.targetMaxHp = r.ReadUInt32();
        out.killed = r.ReadBool();
        out.serverTime = r.ReadUInt64();
        out.sourceType = r.ReadUInt8();
        out.sourceId = r.ReadUInt64();
    });
}

bool EncodeEntityHealthSnapshot(const EntityHealthSnapshotPayload& p,
                                std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt8(p.entityType);
        w.WriteUInt64(p.entityId);
        w.WriteUInt32(p.currentHp);
        w.WriteUInt32(p.maxHp);
        w.WriteBool(p.alive);
        w.WriteUInt64(p.serverTime);
        return true;
    });
}

bool DecodeEntityHealthSnapshot(const std::uint8_t* data, std::size_t size,
                                EntityHealthSnapshotPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.entityType = r.ReadUInt8();
        out.entityId = r.ReadUInt64();
        out.currentHp = r.ReadUInt32();
        out.maxHp = r.ReadUInt32();
        out.alive = r.ReadBool();
        out.serverTime = r.ReadUInt64();
    });
}

bool EncodeMonsterDeath(const MonsterDeathPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.entityId);
        w.WriteUInt64(p.killerCharacterId);
        w.WriteUInt64(p.serverTime);
        return true;
    });
}

bool DecodeMonsterDeath(const std::uint8_t* data, std::size_t size, MonsterDeathPayload& out,
                        std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.entityId = r.ReadUInt64();
        out.killerCharacterId = r.ReadUInt64();
        out.serverTime = r.ReadUInt64();
    });
}

bool EncodePlayerDeath(const PlayerDeathPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.characterId);
        w.WriteUInt8(p.killerType);
        w.WriteUInt64(p.killerId);
        w.WriteUInt64(p.serverTime);
        return true;
    });
}

bool DecodePlayerDeath(const std::uint8_t* data, std::size_t size, PlayerDeathPayload& out,
                       std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.characterId = r.ReadUInt64();
        out.killerType = r.ReadUInt8();
        out.killerId = r.ReadUInt64();
        out.serverTime = r.ReadUInt64();
    });
}

} // namespace legend::world
