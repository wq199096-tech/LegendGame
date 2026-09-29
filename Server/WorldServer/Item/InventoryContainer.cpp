#include "Server/WorldServer/Item/InventoryContainer.h"

namespace legend::world {

std::size_t InventoryContainer::UsedCount() const {
    std::size_t used = 0;
    for (const auto& entry : m_slots) {
        if (entry.quantity > 0) {
            ++used;
        }
    }
    return used;
}

InventoryAddResult InventoryContainer::Add(const ItemRegistry& registry,
                                           std::uint32_t definitionId, std::uint32_t quantity,
                                           std::uint64_t instanceId, std::int64_t createdAt) {
    (void)createdAt; // createdAt 由持久化层维护（容器不展示）
    InventoryAddResult result;
    const ItemDefinition* definition = registry.Find(definitionId);
    if (!definition || quantity == 0) {
        result.code = InventoryAddCode::Full;
        result.remainingQuantity = quantity;
        return result;
    }
    std::uint32_t remaining = quantity;
    bool usedNewSlot = false;
    // 指令二十六：优先并入已有同 definition 堆叠（不超过 maxStack）。
    // 只并入已持久化堆叠（instanceId != 0）：待落库堆叠不参与合并，
    // 避免两次连续拾取在 DB 回填前的排序竞争。
    if (definition->maxStack > 1) {
        for (auto& entry : m_slots) {
            if (remaining == 0) {
                break;
            }
            if (entry.quantity > 0 && entry.instanceId != 0 &&
                entry.definitionId == definitionId && entry.quantity < definition->maxStack) {
                const std::uint32_t space = definition->maxStack - entry.quantity;
                const std::uint32_t merged = space < remaining ? space : remaining;
                entry.quantity += merged;
                result.slotIndex = static_cast<std::uint32_t>(&entry - m_slots.data());
                result.addedQuantity += merged;
                remaining -= merged;
            }
        }
    }
    // 其余（或全部）放入新槽；新堆叠携带调用方分配的 instanceId（0 = 待持久化回填）。
    while (remaining > 0) {
        std::size_t freeSlot = 0;
        if (!FirstFreeSlot(freeSlot)) {
            result.code = InventoryAddCode::Full;
            result.remainingQuantity = remaining;
            return result;
        }
        const std::uint32_t put = definition->maxStack < remaining ? definition->maxStack : remaining;
        InventoryEntry entry;
        entry.definitionId = definitionId;
        entry.quantity = put;
        entry.instanceId = instanceId;
        m_slots[freeSlot] = entry;
        if (!usedNewSlot) {
            result.slotIndex = static_cast<std::uint32_t>(freeSlot);
            usedNewSlot = true;
        }
        result.addedQuantity += put;
        remaining -= put;
        instanceId = 0; // 只有第一个新堆叠用该 instanceId
    }
    result.remainingQuantity = 0;
    result.code = usedNewSlot ? InventoryAddCode::Success : InventoryAddCode::Merged;
    return result;
}

bool InventoryContainer::TakeAt(std::size_t slotIndex, InventoryEntry& out) {
    if (slotIndex >= m_slots.size() || m_slots[slotIndex].quantity == 0) {
        return false;
    }
    out = m_slots[slotIndex];
    m_slots[slotIndex] = InventoryEntry{};
    return true;
}

bool InventoryContainer::PutAt(std::size_t slotIndex, const InventoryEntry& entry) {
    if (slotIndex >= m_slots.size()) {
        return false;
    }
    m_slots[slotIndex] = entry;
    return true;
}

bool InventoryContainer::FirstFreeSlot(std::size_t& outSlotIndex) const {
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].quantity == 0) {
            outSlotIndex = i;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// EquipmentService
// ---------------------------------------------------------------------------

EquipResult EquipmentService::Equip(const ItemRegistry& registry, InventoryContainer& bag,
                                    std::size_t slotIndex, EquipmentSlots& equipped) {
    EquipResult result;
    const InventoryEntry* entry = bag.At(slotIndex);
    if (!entry || entry->quantity == 0) {
        result.code = EquipResultCode::InvalidSlot;
        return result;
    }
    const ItemDefinition* definition = registry.Find(entry->definitionId);
    if (!definition || definition->type != ItemType::Weapon &&
                           definition->type != ItemType::Armor) {
        result.code = EquipResultCode::InvalidItem;
        return result;
    }
    if (definition->type == ItemType::Weapon &&
        definition->equipmentSlot != EquipmentSlot::Weapon) {
        result.code = EquipResultCode::WrongSlot;
        return result;
    }
    if (definition->type == ItemType::Armor &&
        definition->equipmentSlot != EquipmentSlot::Armor) {
        result.code = EquipResultCode::WrongSlot;
        return result;
    }
    if (entry->quantity != 1) {
        result.code = EquipResultCode::InvalidItem; // 装备 quantity 固定 1（指令二十七）
        return result;
    }

    InventoryEntry taken;
    (void)bag.TakeAt(slotIndex, taken); // 已校验非空
    InventoryEntry previous;
    bool hadPrevious = false;
    if (definition->type == ItemType::Weapon) {
        if (equipped.weapon.quantity > 0) {
            previous = equipped.weapon;
            hadPrevious = true;
        }
        equipped.weapon = taken;
        result.slot = EquipmentSlot::Weapon;
    } else {
        if (equipped.armor.quantity > 0) {
            previous = equipped.armor;
            hadPrevious = true;
        }
        equipped.armor = taken;
        result.slot = EquipmentSlot::Armor;
    }
    if (hadPrevious) {
        // 指令三十三：旧装备回背包——新装备腾出的原槽保证有位（原子交换）。
        (void)bag.PutAt(slotIndex, previous);
        result.unequipped = previous;
        result.code = EquipResultCode::Replaced;
    } else {
        result.code = EquipResultCode::Success;
    }
    result.equipped = taken;
    return result;
}

EquipResult EquipmentService::Unequip(const ItemRegistry& registry, InventoryContainer& bag,
                                      EquipmentSlot slot, EquipmentSlots& equipped) {
    EquipResult result;
    InventoryEntry* current = nullptr;
    if (slot == EquipmentSlot::Weapon) {
        current = &equipped.weapon;
    } else if (slot == EquipmentSlot::Armor) {
        current = &equipped.armor;
    }
    if (!current || current->quantity == 0) {
        result.code = EquipResultCode::InvalidSlot; // 槽位为空（指令五十八 NotEquipped 语义）
        return result;
    }
    const ItemDefinition* definition = registry.Find(current->definitionId);
    if (!definition) {
        result.code = EquipResultCode::InvalidItem;
        return result;
    }
    std::size_t freeSlot = 0;
    if (!bag.FirstFreeSlot(freeSlot)) {
        result.code = EquipResultCode::BagFull; // 指令三十四：背包满 -> 失败且装备不变
        return result;
    }
    InventoryEntry taken = *current;
    *current = InventoryEntry{};
    (void)bag.PutAt(freeSlot, taken);
    result.code = EquipResultCode::Success;
    result.slot = slot;
    result.unequipped = taken;
    result.bagSlotIndex = static_cast<std::uint32_t>(freeSlot);
    return result;
}

} // namespace legend::world
