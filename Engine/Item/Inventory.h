#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Engine/Item/ItemDefinition.h"
#include "Engine/Item/ItemInstance.h"

namespace legend::item {

// 阶段6：20 格；每格一个 ItemInstance Stack
inline constexpr std::size_t kInventoryCapacity = 20;

// AddItem 结果：requested/added/remaining——GroundLoot 据此决定删除/保留部分/保留全部
struct InventoryAddResult {
    int requested = 0;
    int added = 0;
    int remaining = 0;
};

// 玩家背包：独立组件（PlayerCharacter 组合持有，不做继承扩展）。
// Stack 规则：先补已有未满同 definitionId Stack，再用空格；溢出数量放新格。
class Inventory {
public:
    explicit Inventory(std::size_t capacity = kInventoryCapacity);

    // 加入物品：部分可加入时只加能放下的部分（不丢失剩余——由 remaining 返回）
    InventoryAddResult AddItem(const ItemDefinition& definition, int quantity);
    // 移除物品：先从前面 Slot 扣，扣到 0 清空 Slot；返回实际移除数
    int RemoveItem(const std::string& definitionId, int quantity);

    const ItemInstance* GetSlot(std::size_t index) const; // 空/越界返回 nullptr
    int GetItemCount(const std::string& definitionId) const;
    bool IsFull() const; // 无空格（满）
    std::size_t GetUsedSlots() const { return m_usedSlots; }
    std::size_t GetCapacity() const { return m_slots.size(); }
    void Clear();

private:
    struct Slot {
        std::optional<ItemInstance> item; // 空格语义：nullopt（禁止到处判断 ""）
    };
    std::vector<Slot> m_slots;
    std::size_t m_usedSlots = 0;
};

} // namespace legend::item
