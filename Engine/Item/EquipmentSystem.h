#pragma once

#include <string>

#include "Engine/Item/EquipmentComponent.h"
#include "Engine/Item/Inventory.h"
#include "Engine/Item/ItemDefinition.h"

namespace legend::item {

class ItemDatabase;

// 装备操作结果（reason 供日志；不做 Better/结论判断——阶段7 指令三十五）
struct EquipmentOpResult {
    bool success = false;
    std::string reason; // "ok" / "instance not found" / "invalid definition" / "not equipment" /
                        // "inventory full" / "slot empty"
};

// 装备对比结果：只输出差值（阶段7 指令三十四/三十五——不给 Better 结论）
struct EquipmentComparison {
    float attackDelta = 0.0f;
    float defenseDelta = 0.0f;
    float maxHpDelta = 0.0f;
};

// 装备系统：跨 Inventory/Equipment 的 Equip/Unequip/Swap 事务与装备比较。
// 事务安全（阶段7 指令十五/十六/十七）：
// - Swap：先取新装备（背包腾出空格）→ 再取旧装备 → 旧装备放回腾出的空格 → 新装备入槽；
//   任一步失败全部回滚，不丢任何装备
// - Unequip：背包满时装备放回槽位（不丢装备）
// - 不修改 Character 内部数据（属性重算在 PlayerStatsComponent/PlayerCharacter）
class EquipmentSystem {
public:
    // 装备背包中的实例：取出 → 校验定义 → 槽空则放入；槽有旧装备则事务式 Swap
    static EquipmentOpResult Equip(const ItemDatabase& items, Inventory& inventory,
                                   EquipmentComponent& equipment, ItemInstanceId instanceId);
    // 卸下槽位装备回背包：背包满时装备放回槽位（不丢装备）
    static EquipmentOpResult Unequip(const ItemDatabase& items, Inventory& inventory,
                                     EquipmentComponent& equipment, EquipmentSlotType slot);
    // 装备对比：candidate（待穿）vs current（当前槽内，可为 nullptr=空槽）——只输出差值
    static EquipmentComparison Compare(const ItemDefinition& candidate,
                                       const ItemDefinition* current);
};

} // namespace legend::item
