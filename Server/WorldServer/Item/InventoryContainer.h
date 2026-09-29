#pragma once

#include "Server/WorldServer/Item/ItemRegistry.h"
#include "Shared/Item/ItemDefinition.h"
#include "Shared/Item/ItemTypes.h"

#include <array>
#include <cstdint>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段18 指令六/七/二十五/二十六：InventoryContainer —— 服务器权威 40 格背包。
// runtime 权威状态（持久化经 InventoryRepository 异步落库）；
// 仅 World io 线程访问（无锁）。
// ---------------------------------------------------------------------------

struct InventoryEntry {
    std::uint64_t instanceId = 0; // 0 = 未持久化（写库后回填）
    std::uint32_t definitionId = 0;
    std::uint32_t quantity = 0;   // 0 = 空槽
};

// 添加结果（指令二十五：满时精确区分，Drop 留在地上）。
enum class InventoryAddCode {
    Success,     // 新堆叠入新槽
    Merged,      // 并入已有堆叠
    Full,        // 无空槽且无可合并堆叠
};

struct InventoryAddResult {
    InventoryAddCode code = InventoryAddCode::Full;
    std::uint32_t slotIndex = 0;
    std::uint32_t addedQuantity = 0;
    std::uint32_t remainingQuantity = 0; // Full 时 = 未入库数量
};

class InventoryContainer {
public:
    void Clear() { m_slots.fill(InventoryEntry{}); }

    std::size_t SlotCount() const { return m_slots.size(); }
    const InventoryEntry* At(std::size_t slotIndex) const {
        return slotIndex < m_slots.size() ? &m_slots[slotIndex] : nullptr;
    }
    InventoryEntry* MutableAt(std::size_t slotIndex) {
        return slotIndex < m_slots.size() ? &m_slots[slotIndex] : nullptr;
    }

    std::size_t UsedCount() const;
    bool IsFull() const { return UsedCount() >= m_slots.size(); }

    // 服务器权威添加（指令二十六：同 definition 优先堆叠 -> 新格；不超 maxStack）。
    InventoryAddResult Add(const ItemRegistry& registry, std::uint32_t definitionId,
                           std::uint32_t quantity, std::uint64_t instanceId, std::int64_t createdAt);

    // 取出整个槽（装备/卸下用；调用方负责持久化）。
    bool TakeAt(std::size_t slotIndex, InventoryEntry& out);
    // 直接写入槽（重启加载/装备回包用）。
    bool PutAt(std::size_t slotIndex, const InventoryEntry& entry);
    // 找到第一个空槽；满返回 false。
    bool FirstFreeSlot(std::size_t& outSlotIndex) const;

private:
    std::array<InventoryEntry, kInventorySlots> m_slots{};
};

// ---------------------------------------------------------------------------
// 阶段18 指令三十/三十三/三十四：EquipmentService —— 装备/卸下纯逻辑。
// 装备槽状态（Weapon/Armor 两槽）存于 PlayerSession；本服务只做校验与
// Inventory/Equipment 状态变更（WorldServer 编排持久化与 Derived 重算）。
// ---------------------------------------------------------------------------
struct EquipmentSlots {
    InventoryEntry weapon; // quantity 恒 1（指令二十七）
    InventoryEntry armor;
};

enum class EquipResultCode {
    Success,
    Replaced,        // 旧装备回背包（原子交换）
    InvalidSlot,     // slotIndex 越界 / 空槽
    InvalidItem,     // definition 不存在 / 非装备
    WrongSlot,       // 物品类型与目标槽不匹配
    BagFull,         // 理论不发生（交换用原槽），防御保留
};

struct EquipResult {
    EquipResultCode code = EquipResultCode::InvalidSlot;
    EquipmentSlot slot = EquipmentSlot::None;
    InventoryEntry equipped;  // 成功时：新装备
    InventoryEntry unequipped; // Replaced 时：旧装备（已放回背包）
    std::uint32_t bagSlotIndex = 0; // 卸下落点（Unequip 用）
};

class EquipmentService {
public:
    // 装备背包 slotIndex 上的物品（原子：交换在同一 io 线程步骤内完成）。
    static EquipResult Equip(const ItemRegistry& registry, InventoryContainer& bag,
                             std::size_t slotIndex, EquipmentSlots& equipped);
    // 卸下到背包空槽（满 -> 失败，装备保持不变，指令三十四）。
    static EquipResult Unequip(const ItemRegistry& registry, InventoryContainer& bag,
                               EquipmentSlot slot, EquipmentSlots& equipped);
};

} // namespace legend::world
