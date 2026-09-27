#include "Engine/Item/EquipmentComponent.h"

#include "Engine/Item/ItemDatabase.h"

namespace legend::item {

const ItemInstance* EquipmentComponent::GetEquipped(EquipmentSlotType slot) const {
    const int index = static_cast<int>(slot);
    if (index < 0 || index >= kEquipmentSlotCount) {
        return nullptr;
    }
    const auto& slotData = m_slots[index];
    return slotData.has_value() ? &slotData.value() : nullptr;
}

std::optional<ItemInstance> EquipmentComponent::TakeEquipped(EquipmentSlotType slot) {
    const int index = static_cast<int>(slot);
    if (index < 0 || index >= kEquipmentSlotCount) {
        return std::nullopt;
    }
    auto& slotData = m_slots[index];
    if (!slotData.has_value()) {
        return std::nullopt; // 空槽
    }
    ItemInstance taken = std::move(*slotData); // instanceId 保持
    slotData.reset();
    return taken;
}

bool EquipmentComponent::TrySetEquipped(EquipmentSlotType slot, const ItemInstance& instance) {
    const int index = static_cast<int>(slot);
    if (index < 0 || index >= kEquipmentSlotCount) {
        return false;
    }
    // 阶段7.1 防覆盖：槽非空时拒绝（覆盖会静默丢失旧装备——必须走 TakeEquipped 流程）
    if (m_slots[index].has_value()) {
        return false;
    }
    m_slots[index] = instance;
    return true;
}

bool EquipmentComponent::ContainsInstanceId(ItemInstanceId instanceId) const {
    for (const auto& slot : m_slots) {
        if (slot.has_value() && slot->instanceId == instanceId) {
            return true;
        }
    }
    return false;
}

EquipmentBonuses EquipmentComponent::CalculateBonuses(const ItemDatabase& items) const {
    EquipmentBonuses total;
    for (const auto& slot : m_slots) {
        if (!slot.has_value()) {
            continue;
        }
        const ItemDefinition* definition = items.Get(slot->definitionId);
        if (definition == nullptr || !definition->hasEquipment) {
            continue; // 未知定义（不应发生——Loot 只生成 ItemDatabase 内的物品）
        }
        total.attack += definition->equipment.attackBonus;
        total.defense += definition->equipment.defenseBonus;
        total.maxHp += definition->equipment.maxHpBonus;
    }
    return total;
}

int EquipmentComponent::GetEquippedCount() const {
    int count = 0;
    for (const auto& slot : m_slots) {
        if (slot.has_value()) {
            ++count;
        }
    }
    return count;
}

} // namespace legend::item
