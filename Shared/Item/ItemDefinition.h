#pragma once

#include "Shared/Item/ItemTypes.h"

#include <cstdint>
#include <string>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段18：ItemDefinition（静态定义，阶段23 起数据驱动 Data/Game/items.json，
// 23.3：canBuy/canSell/iconKey/enabled）与 ItemInstance（运行时实例，指令四）。
// 阶段18 无随机词条（指令四）：同 definition 的非堆叠物品数值完全一致。
// ---------------------------------------------------------------------------

struct ItemDefinition {
    std::uint32_t definitionId = 0;
    std::string name;                // 阶段23：const char* → std::string（数据驱动）
    ItemType type = ItemType::None;
    std::uint32_t maxStack = 1;      // Weapon/Armor=1；Slime Core=99
    std::uint32_t attackBonus = 0;   // Rusty Sword +3（指令三）
    std::uint32_t defenseBonus = 0;  // Cloth Armor +2
    // 类型 -> 默认装备槽（Material 无槽）。
    EquipmentSlot equipmentSlot = EquipmentSlot::None;
    // 阶段23 23.3：数据驱动字段。
    bool canBuy = true;
    bool canSell = true;
    std::string iconKey;
    bool enabled = true;
};

// 背包内/掉落中的物品实例。instanceId 由 SQLite INTEGER PRIMARY KEY 生成
//（指令九：进程内计数不能做持久化 id，重启不能碰撞）。
struct ItemInstance {
    std::uint64_t instanceId = 0;    // 0 = 尚未持久化（拾取写库后回填）
    std::uint32_t definitionId = 0;
    std::uint32_t quantity = 1;
    std::int64_t createdAt = 0;      // unix 秒
};

} // namespace legend::world
