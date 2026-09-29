#pragma once

#include "Shared/Item/ItemProtocol.h"
#include "Shared/Item/ItemDefinition.h"
#include "Shared/Item/ItemTypes.h"

#include <array>
#include <cstdint>
#include <map>
#include <unordered_map>

namespace legend::client {

// ---------------------------------------------------------------------------
// 阶段18 指令四十四：客户端物品镜像模型（Client 只是镜像，指令二十九——
// 全部变更来自 WorldItemSpawn/Despawn、InventorySnapshot/Delta、EquipmentSnapshot）。
// ---------------------------------------------------------------------------

// 世界掉落镜像（RemoteWorldItem）。
struct RemoteWorldItem {
    std::uint64_t dropEntityId = 0;
    std::uint32_t itemDefinitionId = 0;
    std::uint32_t quantity = 1;
    std::uint16_t mapId = 1;
    float x = 0.0f;
    float y = 0.0f;
    bool isOwnedByYou = false;
    std::uint32_t ownerLockRemainingMs = 0;
};

// 指令四十四：RemoteWorldItemManager —— 只维护服务器 Spawn 过的掉落
//（指令四十八：Snapshot 不存在时绝不凭空创建；Despawn 即移除）。
class RemoteWorldItemManager {
public:
    void Clear() { m_items.clear(); }
    std::size_t Count() const { return m_items.size(); }
    const RemoteWorldItem* Find(std::uint64_t dropEntityId) const {
        const auto it = m_items.find(dropEntityId);
        return it != m_items.end() ? &it->second : nullptr;
    }

    void OnSpawn(const RemoteWorldItem& item) { m_items[item.dropEntityId] = item; }
    bool OnDespawn(std::uint64_t dropEntityId) { return m_items.erase(dropEntityId) != 0; }

    const std::unordered_map<std::uint64_t, RemoteWorldItem>& Items() const { return m_items; }

private:
    std::unordered_map<std::uint64_t, RemoteWorldItem> m_items;
};

// 背包槽镜像。
struct ClientInventorySlot {
    std::uint64_t instanceId = 0;
    std::uint32_t definitionId = 0;
    std::uint32_t quantity = 0; // 0 = 空
};

// 指令四十四：ClientInventoryModel —— 40 格镜像。
class ClientInventoryModel {
public:
    static constexpr std::size_t kSlots = world::kInventorySlots;

    void Clear() { m_slots.fill(ClientInventorySlot{}); }
    std::size_t UsedCount() const {
        std::size_t used = 0;
        for (const auto& slot : m_slots) {
            if (slot.quantity > 0) {
                ++used;
            }
        }
        return used;
    }
    const ClientInventorySlot& Slot(std::size_t index) const { return m_slots[index]; }

    // Snapshot：全量覆盖（指令二十八）。
    void ApplySnapshot(const std::vector<world::InventoryEntryData>& entries) {
        Clear();
        for (const auto& entry : entries) {
            if (entry.slotIndex < kSlots) {
                auto& slot = m_slots[entry.slotIndex];
                slot.instanceId = entry.instanceId;
                slot.definitionId = entry.definitionId;
                slot.quantity = entry.quantity;
            }
        }
    }
    // Delta opcode 1 = Set（新增/合并覆盖该槽）/ 2 = Remove。
    void ApplyDelta(std::uint8_t opcode, const world::InventoryEntryData& entry) {
        if (entry.slotIndex >= kSlots) {
            return;
        }
        auto& slot = m_slots[entry.slotIndex];
        if (opcode == 1) {
            slot.instanceId = entry.instanceId;
            slot.definitionId = entry.definitionId;
            slot.quantity = entry.quantity;
        } else if (opcode == 2) {
            slot = ClientInventorySlot{};
        }
    }

private:
    std::array<ClientInventorySlot, kSlots> m_slots{};
};

// 指令四十四：ClientEquipmentModel —— Weapon/Armor 槽镜像。
class ClientEquipmentModel {
public:
    void Clear() {
        m_weaponInstanceId = 0;
        m_weaponDefinitionId = 0;
        m_armorInstanceId = 0;
        m_armorDefinitionId = 0;
        m_attackBonus = 0;
        m_defenseBonus = 0;
    }
    void ApplySnapshot(const world::EquipmentSnapshotPayload& payload) {
        m_weaponInstanceId = payload.weaponInstanceId;
        m_weaponDefinitionId = payload.weaponDefinitionId;
        m_armorInstanceId = payload.armorInstanceId;
        m_armorDefinitionId = payload.armorDefinitionId;
        m_attackBonus = payload.equipmentAttackBonus;
        m_defenseBonus = payload.equipmentDefenseBonus;
    }
    std::uint64_t WeaponInstanceId() const { return m_weaponInstanceId; }
    std::uint32_t WeaponDefinitionId() const { return m_weaponDefinitionId; }
    std::uint64_t ArmorInstanceId() const { return m_armorInstanceId; }
    std::uint32_t ArmorDefinitionId() const { return m_armorDefinitionId; }
    std::uint32_t AttackBonus() const { return m_attackBonus; }
    std::uint32_t DefenseBonus() const { return m_defenseBonus; }

private:
    std::uint64_t m_weaponInstanceId = 0;
    std::uint32_t m_weaponDefinitionId = 0;
    std::uint64_t m_armorInstanceId = 0;
    std::uint32_t m_armorDefinitionId = 0;
    std::uint32_t m_attackBonus = 0;
    std::uint32_t m_defenseBonus = 0;
};

} // namespace legend::client
