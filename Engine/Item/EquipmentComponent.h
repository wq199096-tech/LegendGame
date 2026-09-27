#pragma once

#include <optional>

#include "Engine/Item/ItemDefinition.h"
#include "Engine/Item/ItemInstance.h"

namespace legend::item {

class ItemDatabase;

// 装备加成汇总（阶段7：attack/defense/maxHp 三项，与 CombatStats 的 float 一致）
struct EquipmentBonuses {
    float attack = 0.0f;
    float defense = 0.0f;
    float maxHp = 0.0f;
};

// 装备栏组件（属于 Player，6 槽；每槽 std::optional<ItemInstance>）。
// 职责边界（阶段7 指令七十三）：
// - 只维护装备槽里的实例（放入/取出/加成汇总），实例 ownership 随槽转移
// - 不加载 ItemDatabase、不持有 Inventory、不做 Combat damage
// - 跨 Inventory/Equipment 的 Equip/Unequip/Swap 事务在 EquipmentSystem
// - 不直接修改 Character 内部数据（属性重算在 PlayerStatsComponent）
class EquipmentComponent {
public:
    // 槽内实例（空槽返回 nullptr）
    const ItemInstance* GetEquipped(EquipmentSlotType slot) const;
    // 槽是否为空
    bool IsSlotEmpty(EquipmentSlotType slot) const { return GetEquipped(slot) == nullptr; }
    // 取出槽内实例（槽变空）；空槽返回 nullopt
    std::optional<ItemInstance> TakeEquipped(EquipmentSlotType slot);
    // 放入实例（调用方保证槽为空——Swap 时先取出旧装备）
    void SetEquipped(EquipmentSlotType slot, const ItemInstance& instance);
    // 遍历全部槽位汇总加成（按 definitionId 查 ItemDefinition 的 equipment 块）
    EquipmentBonuses CalculateBonuses(const ItemDatabase& items) const;
    // 当前已装备槽位数（0~6，Debug 用）
    int GetEquippedCount() const;
    // 全槽扫描（Unique check 用）：对每个已装备实例调用回调
    template <typename Fn>
    void ForEachEquipped(Fn&& fn) const {
        for (const auto& slot : m_slots) {
            if (slot.has_value()) {
                fn(*slot);
            }
        }
    }

private:
    // 按 EquipmentSlotType 枚举顺序排列的 6 个槽
    std::optional<ItemInstance> m_slots[kEquipmentSlotCount];
};

} // namespace legend::item
