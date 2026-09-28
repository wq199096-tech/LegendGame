#include "Shared/Monster/MonsterProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {
namespace {

// 与 WorldProtocol 相同模式：Encode 失败清空 out；Decode 必须完整消费（指令六十三）。
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
        error = "malformed monster payload";
        return false;
    }
    if (reader.Remaining() != 0) {
        error = "malformed monster payload (trailing bytes)";
        return false;
    }
    return true;
}

} // namespace

bool EncodeMonsterSpawn(const MonsterSpawnPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.entityId);
        w.WriteUInt32(p.monsterTypeId);
        w.WriteUInt32(p.level);
        w.WriteUInt16(p.mapId);
        w.WriteFloat(p.positionX);
        w.WriteFloat(p.positionY);
        w.WriteUInt8(p.state);
        w.WriteUInt64(p.serverTime);
        return w.WriteString(p.name);
    });
}

bool DecodeMonsterSpawn(const std::uint8_t* data, std::size_t size, MonsterSpawnPayload& out,
                        std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.entityId = r.ReadUInt64();
        out.monsterTypeId = r.ReadUInt32();
        out.level = r.ReadUInt32();
        out.mapId = r.ReadUInt16();
        out.positionX = r.ReadFloat();
        out.positionY = r.ReadFloat();
        out.state = r.ReadUInt8();
        out.serverTime = r.ReadUInt64();
        (void)(r.ReadString(out.name));
    });
}

bool EncodeMonsterDespawn(const MonsterDespawnPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.entityId);
        w.WriteUInt8(p.reason);
        return true;
    });
}

bool DecodeMonsterDespawn(const std::uint8_t* data, std::size_t size, MonsterDespawnPayload& out,
                          std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.entityId = r.ReadUInt64();
        out.reason = r.ReadUInt8();
    });
}

bool EncodeMonsterBatchSnapshot(const MonsterBatchSnapshotPayload& p,
                                std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.serverTime);
        w.WriteUInt16(static_cast<std::uint16_t>(p.monsters.size()));
        for (const auto& entry : p.monsters) {
            w.WriteUInt64(entry.entityId);
            w.WriteFloat(entry.positionX);
            w.WriteFloat(entry.positionY);
            w.WriteUInt8(entry.state);
            w.WriteUInt64(entry.targetCharacterId);
        }
        return true;
    });
}

bool DecodeMonsterBatchSnapshot(const std::uint8_t* data, std::size_t size,
                                MonsterBatchSnapshotPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.serverTime = r.ReadUInt64();
        const std::uint16_t count = r.ReadUInt16();
        // 指令二十二/六十四：count>128 拒绝；count 超剩余 payload 越界即失败。
        if (count > kMonsterBatchMaxMonsters) {
            r.Invalidate();
            return;
        }
        out.monsters.reserve(count);
        for (std::uint16_t i = 0; i < count; ++i) {
            MonsterSnapshotEntry entry;
            entry.entityId = r.ReadUInt64();
            entry.positionX = r.ReadFloat();
            entry.positionY = r.ReadFloat();
            entry.state = r.ReadUInt8();
            entry.targetCharacterId = r.ReadUInt64();
            out.monsters.push_back(entry);
        }
    });
}

} // namespace legend::world
