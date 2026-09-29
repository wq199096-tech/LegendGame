#include "Shared/Item/ItemProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {

namespace {
using legend::network::ByteReader;
using legend::network::ByteWriter;

// InventorySnapshot 条目数安全上限：40 格 + 余量（恶意 count 直接拒绝）。
inline constexpr std::uint32_t kMaxInventoryEntries = 64;
} // namespace

const char* ItemResultCodeName(std::uint8_t code) {
    switch (static_cast<ItemResultCode>(code)) {
        case ItemResultCode::None: return "None";
        case ItemResultCode::Success: return "Success";
        case ItemResultCode::MalformedRequest: return "MalformedRequest";
        case ItemResultCode::NotInWorld: return "NotInWorld";
        case ItemResultCode::Dead: return "Dead";
        case ItemResultCode::DropNotFound: return "DropNotFound";
        case ItemResultCode::WrongMap: return "WrongMap";
        case ItemResultCode::NotVisible: return "NotVisible";
        case ItemResultCode::TooFar: return "TooFar";
        case ItemResultCode::OwnerLocked: return "OwnerLocked";
        case ItemResultCode::InventoryFull: return "InventoryFull";
        case ItemResultCode::DuplicateRequest: return "DuplicateRequest";
        case ItemResultCode::InvalidItem: return "InvalidItem";
        case ItemResultCode::WrongSlot: return "WrongSlot";
        case ItemResultCode::NotEquipped: return "NotEquipped";
        case ItemResultCode::InternalError: return "InternalError";
    }
    return "Unknown";
}

bool EncodeWorldItemSpawn(const WorldItemSpawnPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.dropEntityId);
    w.WriteUInt32(p.itemDefinitionId);
    w.WriteUInt32(p.quantity);
    w.WriteUInt16(p.mapId);
    w.WriteFloat(p.x);
    w.WriteFloat(p.y);
    w.WriteBool(p.isOwnedByYou);
    w.WriteUInt32(p.ownerLockRemainingMs);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeWorldItemSpawn(const std::uint8_t* data, std::size_t size,
                          WorldItemSpawnPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.dropEntityId = r.ReadUInt64();
    out.itemDefinitionId = r.ReadUInt32();
    out.quantity = r.ReadUInt32();
    out.mapId = r.ReadUInt16();
    out.x = r.ReadFloat();
    out.y = r.ReadFloat();
    out.isOwnedByYou = r.ReadBool();
    out.ownerLockRemainingMs = r.ReadUInt32();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed WorldItemSpawn payload";
        return false;
    }
    return true;
}

bool EncodeWorldItemDespawn(const WorldItemDespawnPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.dropEntityId);
    w.WriteUInt8(p.reason);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeWorldItemDespawn(const std::uint8_t* data, std::size_t size,
                            WorldItemDespawnPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.dropEntityId = r.ReadUInt64();
    out.reason = r.ReadUInt8();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed WorldItemDespawn payload";
        return false;
    }
    return true;
}

bool EncodeItemPickupRequest(const ItemPickupRequestPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt64(p.dropEntityId);
    return true;
}

bool DecodeItemPickupRequest(const std::uint8_t* data, std::size_t size,
                             ItemPickupRequestPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.dropEntityId = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed ItemPickupRequest payload";
        return false;
    }
    return true;
}

bool EncodeItemPickupResponse(const ItemPickupResponsePayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt64(p.dropEntityId);
    w.WriteBool(p.success);
    w.WriteUInt8(p.resultCode);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeItemPickupResponse(const std::uint8_t* data, std::size_t size,
                              ItemPickupResponsePayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.dropEntityId = r.ReadUInt64();
    out.success = r.ReadBool();
    out.resultCode = r.ReadUInt8();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed ItemPickupResponse payload";
        return false;
    }
    return true;
}

bool EncodeInventorySnapshot(const InventorySnapshotPayload& p, std::vector<std::uint8_t>& out) {
    if (p.entries.size() > kMaxInventoryEntries) {
        return false;
    }
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.characterId);
    w.WriteUInt32(static_cast<std::uint32_t>(p.entries.size()));
    for (const auto& entry : p.entries) {
        w.WriteUInt64(entry.instanceId);
        w.WriteUInt32(entry.definitionId);
        w.WriteUInt32(entry.quantity);
        w.WriteUInt32(entry.slotIndex);
    }
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeInventorySnapshot(const std::uint8_t* data, std::size_t size,
                             InventorySnapshotPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.characterId = r.ReadUInt64();
    const std::uint32_t count = r.ReadUInt32();
    if (count > kMaxInventoryEntries) {
        error = "malformed InventorySnapshot payload (entry count)";
        return false;
    }
    out.entries.clear();
    out.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        InventoryEntryData entry;
        entry.instanceId = r.ReadUInt64();
        entry.definitionId = r.ReadUInt32();
        entry.quantity = r.ReadUInt32();
        entry.slotIndex = r.ReadUInt32();
        out.entries.push_back(entry);
    }
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed InventorySnapshot payload";
        return false;
    }
    return true;
}

bool EncodeInventoryDelta(const InventoryDeltaPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.characterId);
    w.WriteUInt8(p.opcode);
    w.WriteUInt64(p.entry.instanceId);
    w.WriteUInt32(p.entry.definitionId);
    w.WriteUInt32(p.entry.quantity);
    w.WriteUInt32(p.entry.slotIndex);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeInventoryDelta(const std::uint8_t* data, std::size_t size,
                          InventoryDeltaPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.characterId = r.ReadUInt64();
    out.opcode = r.ReadUInt8();
    out.entry.instanceId = r.ReadUInt64();
    out.entry.definitionId = r.ReadUInt32();
    out.entry.quantity = r.ReadUInt32();
    out.entry.slotIndex = r.ReadUInt32();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed InventoryDelta payload";
        return false;
    }
    return true;
}

bool EncodeEquipItemRequest(const EquipItemRequestPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt32(p.slotIndex);
    return true;
}

bool DecodeEquipItemRequest(const std::uint8_t* data, std::size_t size,
                            EquipItemRequestPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.slotIndex = r.ReadUInt32();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed EquipItemRequest payload";
        return false;
    }
    return true;
}

bool EncodeEquipItemResponse(const EquipItemResponsePayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteBool(p.success);
    w.WriteUInt8(p.resultCode);
    w.WriteUInt8(p.equipmentSlot);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeEquipItemResponse(const std::uint8_t* data, std::size_t size,
                             EquipItemResponsePayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.success = r.ReadBool();
    out.resultCode = r.ReadUInt8();
    out.equipmentSlot = r.ReadUInt8();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed EquipItemResponse payload";
        return false;
    }
    return true;
}

bool EncodeUnequipItemRequest(const UnequipItemRequestPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt8(p.equipmentSlot);
    return true;
}

bool DecodeUnequipItemRequest(const std::uint8_t* data, std::size_t size,
                              UnequipItemRequestPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.equipmentSlot = r.ReadUInt8();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed UnequipItemRequest payload";
        return false;
    }
    return true;
}

bool EncodeUnequipItemResponse(const UnequipItemResponsePayload& p,
                               std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteBool(p.success);
    w.WriteUInt8(p.resultCode);
    w.WriteUInt8(p.equipmentSlot);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeUnequipItemResponse(const std::uint8_t* data, std::size_t size,
                               UnequipItemResponsePayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.success = r.ReadBool();
    out.resultCode = r.ReadUInt8();
    out.equipmentSlot = r.ReadUInt8();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed UnequipItemResponse payload";
        return false;
    }
    return true;
}

bool EncodeEquipmentSnapshot(const EquipmentSnapshotPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.characterId);
    w.WriteUInt64(p.weaponInstanceId);
    w.WriteUInt32(p.weaponDefinitionId);
    w.WriteUInt64(p.armorInstanceId);
    w.WriteUInt32(p.armorDefinitionId);
    w.WriteUInt32(p.equipmentAttackBonus);
    w.WriteUInt32(p.equipmentDefenseBonus);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeEquipmentSnapshot(const std::uint8_t* data, std::size_t size,
                             EquipmentSnapshotPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.characterId = r.ReadUInt64();
    out.weaponInstanceId = r.ReadUInt64();
    out.weaponDefinitionId = r.ReadUInt32();
    out.armorInstanceId = r.ReadUInt64();
    out.armorDefinitionId = r.ReadUInt32();
    out.equipmentAttackBonus = r.ReadUInt32();
    out.equipmentDefenseBonus = r.ReadUInt32();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed EquipmentSnapshot payload";
        return false;
    }
    return true;
}

} // namespace legend::world
