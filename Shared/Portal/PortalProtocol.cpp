#include "Shared/Portal/PortalProtocol.h"

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

// PortalSpawn(340)（指令十七）。
bool EncodePortalSpawn(std::vector<std::uint8_t>& out, std::uint64_t portalEntityId,
                       std::uint32_t portalId, const std::string& name, std::uint16_t mapId,
                       float x, float y, float interactionRadius,
                       std::uint16_t destinationMapId, const std::string& destinationName) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(portalEntityId);
    w.WriteUInt32(portalId);
    if (!w.WriteString(name)) {
        return false;
    }
    w.WriteUInt16(mapId);
    w.WriteFloat(x);
    w.WriteFloat(y);
    w.WriteFloat(interactionRadius);
    w.WriteUInt16(destinationMapId);
    if (!w.WriteString(destinationName)) {
        return false;
    }
    return true;
}

bool DecodePortalSpawn(const std::uint8_t* data, std::size_t size, std::uint64_t& portalEntityId,
                       std::uint32_t& portalId, std::string& name, std::uint16_t& mapId, float& x,
                       float& y, float& interactionRadius, std::uint16_t& destinationMapId,
                       std::string& destinationName, std::string& error) {
    ByteReader r(data, size);
    portalEntityId = r.ReadUInt64();
    portalId = r.ReadUInt32();
    if (!r.ReadString(name)) {
        error = "malformed PortalSpawn name";
        return false;
    }
    mapId = r.ReadUInt16();
    x = r.ReadFloat();
    y = r.ReadFloat();
    interactionRadius = r.ReadFloat();
    destinationMapId = r.ReadUInt16();
    if (!r.ReadString(destinationName)) {
        error = "malformed PortalSpawn destinationName";
        return false;
    }
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed PortalSpawn payload";
        return false;
    }
    return true;
}

// PortalDespawn(341)。
bool EncodePortalDespawn(std::vector<std::uint8_t>& out, std::uint64_t portalEntityId,
                         std::uint8_t reason) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(portalEntityId);
    w.WriteUInt8(reason);
    return true;
}

bool DecodePortalDespawn(const std::uint8_t* data, std::size_t size,
                         std::uint64_t& portalEntityId, std::uint8_t& reason, std::string& error) {
    ByteReader r(data, size);
    portalEntityId = r.ReadUInt64();
    reason = r.ReadUInt8();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed PortalDespawn payload";
        return false;
    }
    return true;
}

// PortalUseRequest(342)（指令二十）。
bool EncodePortalUseRequest(const PortalUseRequestPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt64(p.portalEntityId);
    return true;
}

bool DecodePortalUseRequest(const std::uint8_t* data, std::size_t size,
                            PortalUseRequestPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.portalEntityId = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed PortalUseRequest payload";
        return false;
    }
    return true;
}

// PortalUseResponse(343)（指令二十二）。
bool EncodePortalUseResponse(const PortalUseResponsePayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteBool(p.success);
    w.WriteUInt8(p.resultCode);
    w.WriteUInt16(p.sourceMapId);
    w.WriteUInt16(p.destinationMapId);
    w.WriteFloat(p.destinationX);
    w.WriteFloat(p.destinationY);
    w.WriteUInt32(p.goldCost);
    w.WriteUInt64(ToBits(p.newGold));
    return true;
}

bool DecodePortalUseResponse(const std::uint8_t* data, std::size_t size,
                             PortalUseResponsePayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.success = r.ReadBool();
    out.resultCode = r.ReadUInt8();
    out.sourceMapId = r.ReadUInt16();
    out.destinationMapId = r.ReadUInt16();
    out.destinationX = r.ReadFloat();
    out.destinationY = r.ReadFloat();
    out.goldCost = r.ReadUInt32();
    out.newGold = FromBits(r.ReadUInt64());
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed PortalUseResponse payload";
        return false;
    }
    return true;
}

} // namespace legend::world
