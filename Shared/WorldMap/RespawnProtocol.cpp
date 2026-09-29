#include "Shared/WorldMap/RespawnProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {

namespace {
using legend::network::ByteReader;
using legend::network::ByteWriter;
// ByteWriter/ByteReader 无 Int64 接口——gold 非负（<2^63），用 UInt64 位模式传输。
inline std::uint64_t ToBits(std::int64_t v) { return static_cast<std::uint64_t>(v); }
inline std::int64_t FromBits(std::uint64_t v) { return static_cast<std::int64_t>(v); }
} // namespace

// RespawnRequest(346)（指令三十三）。
bool EncodeRespawnRequest(const RespawnRequestPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt8(p.respawnMode);
    return true;
}

bool DecodeRespawnRequest(const std::uint8_t* data, std::size_t size, RespawnRequestPayload& out,
                          std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.respawnMode = r.ReadUInt8();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed RespawnRequest payload";
        return false;
    }
    return true;
}

// RespawnResponse(347)。
bool EncodeRespawnResponse(const RespawnResponsePayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteBool(p.success);
    w.WriteUInt8(p.resultCode);
    w.WriteUInt16(p.mapId);
    w.WriteFloat(p.x);
    w.WriteFloat(p.y);
    w.WriteUInt32(p.goldCost);
    w.WriteUInt64(ToBits(p.newGold));
    return true;
}

bool DecodeRespawnResponse(const std::uint8_t* data, std::size_t size, RespawnResponsePayload& out,
                           std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.success = r.ReadBool();
    out.resultCode = r.ReadUInt8();
    out.mapId = r.ReadUInt16();
    out.x = r.ReadFloat();
    out.y = r.ReadFloat();
    out.goldCost = r.ReadUInt32();
    out.newGold = FromBits(r.ReadUInt64());
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed RespawnResponse payload";
        return false;
    }
    return true;
}

// PlayerRespawned(348)（指令四十二）。
bool EncodePlayerRespawned(const PlayerRespawnedPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt16(p.mapId);
    w.WriteFloat(p.x);
    w.WriteFloat(p.y);
    w.WriteUInt32(p.hp);
    w.WriteUInt32(p.maxHp);
    w.WriteUInt32(p.mana);
    w.WriteUInt32(p.maxMana);
    w.WriteUInt64(ToBits(p.gold));
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodePlayerRespawned(const std::uint8_t* data, std::size_t size, PlayerRespawnedPayload& out,
                           std::string& error) {
    ByteReader r(data, size);
    out.mapId = r.ReadUInt16();
    out.x = r.ReadFloat();
    out.y = r.ReadFloat();
    out.hp = r.ReadUInt32();
    out.maxHp = r.ReadUInt32();
    out.mana = r.ReadUInt32();
    out.maxMana = r.ReadUInt32();
    out.gold = FromBits(r.ReadUInt64());
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed PlayerRespawned payload";
        return false;
    }
    return true;
}

} // namespace legend::world
