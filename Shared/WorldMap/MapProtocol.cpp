#include "Shared/WorldMap/MapProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {

namespace {
using legend::network::ByteReader;
using legend::network::ByteWriter;
} // namespace

// MapChanged(344)（指令二十六）。
bool EncodeMapChanged(const MapChangedPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt16(p.mapId);
    if (!w.WriteString(p.mapName)) {
        return false;
    }
    w.WriteFloat(p.x);
    w.WriteFloat(p.y);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeMapChanged(const std::uint8_t* data, std::size_t size, MapChangedPayload& out,
                      std::string& error) {
    ByteReader r(data, size);
    out.mapId = r.ReadUInt16();
    if (!r.ReadString(out.mapName)) {
        error = "malformed MapChanged name";
        return false;
    }
    out.x = r.ReadFloat();
    out.y = r.ReadFloat();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed MapChanged payload";
        return false;
    }
    return true;
}

// MapSnapshot(345)（指令二十七）。
bool EncodeMapSnapshot(const MapSnapshotPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt16(p.mapId);
    if (!w.WriteString(p.mapName)) {
        return false;
    }
    w.WriteFloat(p.minX);
    w.WriteFloat(p.minY);
    w.WriteFloat(p.maxX);
    w.WriteFloat(p.maxY);
    w.WriteFloat(p.spawnX);
    w.WriteFloat(p.spawnY);
    w.WriteFloat(p.respawnX);
    w.WriteFloat(p.respawnY);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeMapSnapshot(const std::uint8_t* data, std::size_t size, MapSnapshotPayload& out,
                       std::string& error) {
    ByteReader r(data, size);
    out.mapId = r.ReadUInt16();
    if (!r.ReadString(out.mapName)) {
        error = "malformed MapSnapshot name";
        return false;
    }
    out.minX = r.ReadFloat();
    out.minY = r.ReadFloat();
    out.maxX = r.ReadFloat();
    out.maxY = r.ReadFloat();
    out.spawnX = r.ReadFloat();
    out.spawnY = r.ReadFloat();
    out.respawnX = r.ReadFloat();
    out.respawnY = r.ReadFloat();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed MapSnapshot payload";
        return false;
    }
    return true;
}

} // namespace legend::world
