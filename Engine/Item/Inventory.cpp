#include "Engine/Item/Inventory.h"

#include <algorithm>

#include "Engine/Debug/Logger.h"

namespace legend::item {

Inventory::Inventory(std::size_t capacity) {
    m_slots.resize(capacity > 0 ? capacity : 1);
}

InventoryAddResult Inventory::AddItem(const ItemDefinition& definition, int quantity) {
    InventoryAddResult result;
    result.requested = quantity > 0 ? quantity : 0;
    if (result.requested == 0 || !definition.IsValid()) {
        result.remaining = result.requested;
        return result;
    }
    int remainingQty = result.requested;

    // 1) 补已有未满同 definitionId Stack（保留已有 instanceId）
    if (definition.maxStack > 1) {
        for (Slot& slot : m_slots) {
            if (remainingQty <= 0) {
                break;
            }
            if (!slot.item.has_value() || slot.item->definitionId != definition.id) {
                continue;
            }
            const int space = definition.maxStack - slot.item->quantity;
            if (space <= 0) {
                continue; // 已满 Stack
            }
            const int moved = std::min(space, remainingQty);
            slot.item->quantity += moved;
            remainingQty -= moved;
            result.added += moved;
        }
    }

    // 2) 空格开新 Stack（新 instanceId；maxStack=1 每件独占一格）
    while (remainingQty > 0) {
        auto empty = std::find_if(m_slots.begin(), m_slots.end(),
                                  [](const Slot& slot) { return !slot.item.has_value(); });
        if (empty == m_slots.end()) {
            break; // 背包满：剩余不丢失，由 remaining 返回
        }
        const int moved = std::min(definition.maxStack, remainingQty);
        ItemInstance instance;
        instance.instanceId = ItemInstanceIdAllocator::Next();
        instance.definitionId = definition.id;
        instance.quantity = moved;
        empty->item = std::move(instance);
        ++m_usedSlots;
        remainingQty -= moved;
        result.added += moved;
    }

    result.remaining = remainingQty;
    return result;
}

int Inventory::RemoveItem(const std::string& definitionId, int quantity) {
    if (quantity <= 0 || definitionId.empty()) {
        return 0;
    }
    int remainingQty = quantity;
    for (Slot& slot : m_slots) {
        if (remainingQty <= 0) {
            break;
        }
        if (!slot.item.has_value() || slot.item->definitionId != definitionId) {
            continue;
        }
        const int moved = std::min(slot.item->quantity, remainingQty);
        slot.item->quantity -= moved;
        remainingQty -= moved;
        if (slot.item->quantity <= 0) {
            slot.item.reset();
            --m_usedSlots;
        }
    }
    return quantity - remainingQty;
}

const ItemInstance* Inventory::GetSlot(std::size_t index) const {
    if (index >= m_slots.size() || !m_slots[index].item.has_value()) {
        return nullptr;
    }
    return &m_slots[index].item.value();
}

int Inventory::GetItemCount(const std::string& definitionId) const {
    int total = 0;
    for (const Slot& slot : m_slots) {
        if (slot.item.has_value() && slot.item->definitionId == definitionId) {
            total += slot.item->quantity;
        }
    }
    return total;
}

// ---- 阶段7：按 instanceId 操作（装备转移，instanceId 全程保持） ----

std::optional<ItemInstance> Inventory::TakeInstance(ItemInstanceId instanceId) {
    for (Slot& slot : m_slots) {
        if (slot.item.has_value() && slot.item->instanceId == instanceId) {
            ItemInstance taken = std::move(*slot.item); // 原实例（instanceId 保持）
            slot.item.reset();
            --m_usedSlots;
            return taken;
        }
    }
    return std::nullopt; // 不存在
}

bool Inventory::AddInstance(const ItemInstance& instance, const ItemDefinition* definition) {
    if (instance.instanceId == 0 || instance.quantity <= 0) {
        return false; // 非法实例（无效 instanceId / 非正数量）
    }
    // Equipment 数量必须为 1（阶段7 指令六十八：拒绝非法实例）
    if (definition != nullptr && definition->type == ItemType::Equipment &&
        instance.quantity != 1) {
        return false;
    }
    // 只找空格放入，保留 instanceId，不并入已有栈（Equipment maxStack=1 每格一件）
    auto empty = std::find_if(m_slots.begin(), m_slots.end(),
                              [](const Slot& slot) { return !slot.item.has_value(); });
    if (empty == m_slots.end()) {
        return false; // 背包满：不生成新 ID，实例由调用方保留
    }
    empty->item = instance;
    ++m_usedSlots;
    return true;
}

const ItemInstance* Inventory::FindByInstanceId(ItemInstanceId instanceId) const {
    for (const Slot& slot : m_slots) {
        if (slot.item.has_value() && slot.item->instanceId == instanceId) {
            return &slot.item.value();
        }
    }
    return nullptr;
}

bool Inventory::IsFull() const {
    return m_usedSlots >= m_slots.size();
}

void Inventory::Clear() {
    for (Slot& slot : m_slots) {
        slot.item.reset();
    }
    m_usedSlots = 0;
}

} // namespace legend::item
