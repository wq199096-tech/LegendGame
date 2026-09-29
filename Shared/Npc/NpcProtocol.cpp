#include "Shared/Npc/NpcProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {

namespace {
using legend::network::ByteReader;
using legend::network::ByteWriter;
} // namespace

// NpcSpawn(320)。
bool EncodeNpcSpawn(std::vector<std::uint8_t>& out, std::uint64_t npcEntityId,
                    std::uint32_t npcDefinitionId, const std::string& name, std::uint16_t mapId,
                    float x, float y, std::uint8_t npcType, std::uint32_t visualId) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(npcEntityId);
    w.WriteUInt32(npcDefinitionId);
    if (!w.WriteString(name)) {
        return false;
    }
    w.WriteUInt16(mapId);
    w.WriteFloat(x);
    w.WriteFloat(y);
    w.WriteUInt8(npcType);
    w.WriteUInt32(visualId);
    return true;
}

bool DecodeNpcSpawn(const std::uint8_t* data, std::size_t size, std::uint64_t& npcEntityId,
                    std::uint32_t& npcDefinitionId, std::string& name, std::uint16_t& mapId,
                    float& x, float& y, std::uint8_t& npcType, std::uint32_t& visualId,
                    std::string& error) {
    ByteReader r(data, size);
    npcEntityId = r.ReadUInt64();
    npcDefinitionId = r.ReadUInt32();
    if (!r.ReadString(name)) {
        error = "malformed NpcSpawn name";
        return false;
    }
    mapId = r.ReadUInt16();
    x = r.ReadFloat();
    y = r.ReadFloat();
    npcType = r.ReadUInt8();
    visualId = r.ReadUInt32();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed NpcSpawn payload";
        return false;
    }
    return true;
}

// NpcDespawn(321)。
bool EncodeNpcDespawn(std::vector<std::uint8_t>& out, std::uint64_t npcEntityId,
                      std::uint8_t reason) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(npcEntityId);
    w.WriteUInt8(reason);
    return true;
}

bool DecodeNpcDespawn(const std::uint8_t* data, std::size_t size, std::uint64_t& npcEntityId,
                      std::uint8_t& reason, std::string& error) {
    ByteReader r(data, size);
    npcEntityId = r.ReadUInt64();
    reason = r.ReadUInt8();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed NpcDespawn payload";
        return false;
    }
    return true;
}

// NpcInteractRequest(322)。
bool EncodeNpcInteractRequest(const NpcInteractRequestPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt64(p.npcEntityId);
    return true;
}

bool DecodeNpcInteractRequest(const std::uint8_t* data, std::size_t size,
                              NpcInteractRequestPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.npcEntityId = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed NpcInteractRequest payload";
        return false;
    }
    return true;
}

// NpcInteractResponse(323)。
bool EncodeNpcInteractResponse(const NpcInteractResponsePayload& p,
                               std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteBool(p.success);
    w.WriteUInt8(p.resultCode);
    w.WriteUInt64(p.npcEntityId);
    w.WriteUInt64(p.dialogueSessionId);
    w.WriteUInt32(p.dialogueId);
    if (!w.WriteString(p.message)) {
        return false;
    }
    return true;
}

bool DecodeNpcInteractResponse(const std::uint8_t* data, std::size_t size,
                               NpcInteractResponsePayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.success = r.ReadBool();
    out.resultCode = r.ReadUInt8();
    out.npcEntityId = r.ReadUInt64();
    out.dialogueSessionId = r.ReadUInt64();
    out.dialogueId = r.ReadUInt32();
    if (!r.ReadString(out.message)) {
        error = "malformed NpcInteractResponse message";
        return false;
    }
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed NpcInteractResponse payload";
        return false;
    }
    return true;
}

// NpcQuestMarkerUpdate(326)。
bool EncodeNpcQuestMarkerUpdate(const NpcQuestMarkerUpdatePayload& p,
                                std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.npcEntityId);
    w.WriteUInt32(p.npcDefinitionId);
    w.WriteUInt8(p.marker);
    return true;
}

bool DecodeNpcQuestMarkerUpdate(const std::uint8_t* data, std::size_t size,
                                NpcQuestMarkerUpdatePayload& out, std::string& error) {
    ByteReader r(data, size);
    out.npcEntityId = r.ReadUInt64();
    out.npcDefinitionId = r.ReadUInt32();
    out.marker = r.ReadUInt8();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed NpcQuestMarkerUpdate payload";
        return false;
    }
    return true;
}

} // namespace legend::world
