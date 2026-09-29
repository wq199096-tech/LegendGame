#include "Shared/Teleport/TeleportProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {

namespace {
using legend::network::ByteReader;
using legend::network::ByteWriter;
inline std::uint64_t ToBits(std::int64_t v) { return static_cast<std::uint64_t>(v); }
inline std::int64_t FromBits(std::uint64_t v) { return static_cast<std::int64_t>(v); }
} // namespace

// TeleportRequest(333)。
bool EncodeTeleportRequest(const TeleportRequestPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt64(p.dialogueSessionId);
    w.WriteUInt32(p.teleportId);
    return true;
}

bool DecodeTeleportRequest(const std::uint8_t* data, std::size_t size, TeleportRequestPayload& out,
                           std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.dialogueSessionId = r.ReadUInt64();
    out.teleportId = r.ReadUInt32();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed TeleportRequest payload";
        return false;
    }
    return true;
}

// TeleportResponse(334)。
bool EncodeTeleportResponse(const TeleportResponsePayload& p, std::vector<std::uint8_t>& out) {
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

bool DecodeTeleportResponse(const std::uint8_t* data, std::size_t size,
                            TeleportResponsePayload& out, std::string& error) {
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
        error = "malformed TeleportResponse payload";
        return false;
    }
    return true;
}

} // namespace legend::world
