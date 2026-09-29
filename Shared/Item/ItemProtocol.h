#pragma once

#include "Shared/Item/ItemTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段18 指令十九：物品/掉落/背包/装备协议 payload。
// MessageId：WorldItemSpawn=290 / WorldItemDespawn=291 / ItemPickupRequest=292 /
// ItemPickupResponse=293 / InventorySnapshot=294 / InventoryDelta=295 /
// EquipItemRequest=296 / EquipItemResponse=297 / UnequipItemRequest=298 /
// UnequipItemResponse=299 / EquipmentSnapshot=300。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0。
// 服务器权威（指令一）：Client 只发 requestId + dropEntityId/slotIndex/槽位
//（指令二十一），绝不能上报 itemDefinitionId/quantity/position/instanceId 生成语义。
// ---------------------------------------------------------------------------

// WorldItemSpawn(290)（指令二十）：不直接暴露完整 owner——用 isOwnedByYou +
// ownerLockRemainingMs。
struct WorldItemSpawnPayload {
    std::uint64_t dropEntityId = 0;
    std::uint32_t itemDefinitionId = 0;
    std::uint32_t quantity = 1;
    std::uint16_t mapId = 1;
    float x = 0.0f;
    float y = 0.0f;
    bool isOwnedByYou = false;
    std::uint32_t ownerLockRemainingMs = 0;
    std::uint64_t serverTime = 0;
};

// WorldItemDespawn(291)（指令四十二：PickedUp/Expired/ServerCleanup）。
struct WorldItemDespawnPayload {
    std::uint64_t dropEntityId = 0;
    std::uint8_t reason = 0; // ItemDespawnReason
    std::uint64_t serverTime = 0;
};

// ItemPickupRequest(292)（指令二十一：Client 只能发 requestId + dropEntityId）。
struct ItemPickupRequestPayload {
    std::uint64_t requestId = 0;
    std::uint64_t dropEntityId = 0;
};

// ItemPickupResponse(293)。
struct ItemPickupResponsePayload {
    std::uint64_t requestId = 0;
    std::uint64_t dropEntityId = 0;
    bool success = false;
    std::uint8_t resultCode = 0; // ItemResultCode
    std::uint64_t serverTime = 0;
};

// InventorySnapshot(294)（指令二十八：进世界下发完整背包）。
struct InventoryEntryData {
    std::uint64_t instanceId = 0;
    std::uint32_t definitionId = 0;
    std::uint32_t quantity = 1;
    std::uint32_t slotIndex = 0; // 0~39
};

struct InventorySnapshotPayload {
    std::uint64_t characterId = 0;
    std::vector<InventoryEntryData> entries;
    std::uint64_t serverTime = 0;
};

// InventoryDelta(295)：opcode 1=Set（新增/合并堆叠覆盖该槽）/ 2=Remove（该槽变空）。
struct InventoryDeltaPayload {
    std::uint64_t characterId = 0;
    std::uint8_t opcode = 0; // 1=Set 2=Remove
    InventoryEntryData entry;
    std::uint64_t serverTime = 0;
};

// EquipItemRequest(296)：Client 发背包槽位（服务器按权威槽内容校验）。
struct EquipItemRequestPayload {
    std::uint64_t requestId = 0;
    std::uint32_t slotIndex = 0;
};

// EquipItemResponse(297)。
struct EquipItemResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint8_t resultCode = 0;   // ItemResultCode
    std::uint8_t equipmentSlot = 0; // EquipmentSlot（成功时）
    std::uint64_t serverTime = 0;
};

// UnequipItemRequest(298)。
struct UnequipItemRequestPayload {
    std::uint64_t requestId = 0;
    std::uint8_t equipmentSlot = 0; // EquipmentSlot
};

// UnequipItemResponse(299)。
struct UnequipItemResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint8_t resultCode = 0;
    std::uint8_t equipmentSlot = 0;
    std::uint64_t serverTime = 0;
};

// EquipmentSnapshot(300)（进世界下发 + 装备变化后全量纠偏）。
struct EquipmentSnapshotPayload {
    std::uint64_t characterId = 0;
    std::uint64_t weaponInstanceId = 0;
    std::uint32_t weaponDefinitionId = 0;
    std::uint64_t armorInstanceId = 0;
    std::uint32_t armorDefinitionId = 0;
    std::uint32_t equipmentAttackBonus = 0; // 汇总加成（客户端展示用）
    std::uint32_t equipmentDefenseBonus = 0;
    std::uint64_t serverTime = 0;
};

bool EncodeWorldItemSpawn(const WorldItemSpawnPayload& p, std::vector<std::uint8_t>& out);
bool DecodeWorldItemSpawn(const std::uint8_t* data, std::size_t size,
                          WorldItemSpawnPayload& out, std::string& error);
bool EncodeWorldItemDespawn(const WorldItemDespawnPayload& p, std::vector<std::uint8_t>& out);
bool DecodeWorldItemDespawn(const std::uint8_t* data, std::size_t size,
                            WorldItemDespawnPayload& out, std::string& error);
bool EncodeItemPickupRequest(const ItemPickupRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeItemPickupRequest(const std::uint8_t* data, std::size_t size,
                             ItemPickupRequestPayload& out, std::string& error);
bool EncodeItemPickupResponse(const ItemPickupResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeItemPickupResponse(const std::uint8_t* data, std::size_t size,
                              ItemPickupResponsePayload& out, std::string& error);
bool EncodeInventorySnapshot(const InventorySnapshotPayload& p, std::vector<std::uint8_t>& out);
bool DecodeInventorySnapshot(const std::uint8_t* data, std::size_t size,
                             InventorySnapshotPayload& out, std::string& error);
bool EncodeInventoryDelta(const InventoryDeltaPayload& p, std::vector<std::uint8_t>& out);
bool DecodeInventoryDelta(const std::uint8_t* data, std::size_t size,
                          InventoryDeltaPayload& out, std::string& error);
bool EncodeEquipItemRequest(const EquipItemRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeEquipItemRequest(const std::uint8_t* data, std::size_t size,
                            EquipItemRequestPayload& out, std::string& error);
bool EncodeEquipItemResponse(const EquipItemResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeEquipItemResponse(const std::uint8_t* data, std::size_t size,
                             EquipItemResponsePayload& out, std::string& error);
bool EncodeUnequipItemRequest(const UnequipItemRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeUnequipItemRequest(const std::uint8_t* data, std::size_t size,
                              UnequipItemRequestPayload& out, std::string& error);
bool EncodeUnequipItemResponse(const UnequipItemResponsePayload& p,
                               std::vector<std::uint8_t>& out);
bool DecodeUnequipItemResponse(const std::uint8_t* data, std::size_t size,
                               UnequipItemResponsePayload& out, std::string& error);
bool EncodeEquipmentSnapshot(const EquipmentSnapshotPayload& p, std::vector<std::uint8_t>& out);
bool DecodeEquipmentSnapshot(const std::uint8_t* data, std::size_t size,
                             EquipmentSnapshotPayload& out, std::string& error);

} // namespace legend::world
