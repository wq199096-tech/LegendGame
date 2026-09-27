#pragma once

#include <cstdint>
#include <string>

namespace legend::item {

// 物品类型：阶段7 扩展 Equipment（保留阶段6 四类；Weapon/Armor 等部位不是 ItemType，
// 由 EquipmentSlotType 单独表示）
enum class ItemType : uint8_t {
    Consumable = 0,
    Material = 1,
    Quest = 2,
    Misc = 3,
    Equipment = 4,
};

const char* ItemTypeName(ItemType type);
ItemType ParseItemType(const std::string& name, ItemType fallback = ItemType::Misc);

// 装备槽位（阶段7：6 槽；Bracelet/Belt/Medal/OffHand 等后续阶段扩展）
enum class EquipmentSlotType : uint8_t {
    Weapon = 0,
    Helmet = 1,
    Armor = 2,
    Necklace = 3,
    Ring = 4,
    Boots = 5,
};

inline constexpr int kEquipmentSlotCount = 6;

// 槽位名（"Weapon"/"Helmet"/"Armor"/"Necklace"/"Ring"/"Boots"）
const char* EquipmentSlotTypeName(EquipmentSlotType slot);
// 字符串解析：未知返回 false（不默认 Weapon——指令六十五）
bool ParseEquipmentSlotType(const std::string& name, EquipmentSlotType& out);

// 装备数据（items.json "equipment" 块）：只有 type==Equipment 的 ItemDefinition 要求此块。
// Bonus 全部 float（与 CombatStats 一致）且 >= 0（阶段7 无负面装备）。
// 预留字段（moveSpeed/attackSpeed/critChance/critDamage）本阶段不使用。
struct EquipmentData {
    EquipmentSlotType slot = EquipmentSlotType::Weapon;
    float attackBonus = 0.0f;
    float defenseBonus = 0.0f;
    float maxHpBonus = 0.0f;

    // slot 枚举合法（Parse 保证）+ 三个 bonus 均非负
    bool IsValid() const;
};

// 物品定义（Assets/Items/items.json）：数据驱动，启动加载一次。
// Equipment 类型的物品必须：maxStack == 1 且带合法 equipment 块（否则该 ItemDefinition 跳过）。
struct ItemDefinition {
    std::string id;         // 如 "wooden_sword"
    std::string name;       // 显示名（阶段7 测试装备可用英文名）
    ItemType type = ItemType::Misc;
    int maxStack = 1;       // Equipment 强制 1
    std::string icon;
    std::string description;

    bool hasEquipment = false;      // type==Equipment 时必须 true
    EquipmentData equipment;        // 装备加成（仅 Equipment 类型有效）

    // id/name 非空 + maxStack >= 1
    bool IsValid() const;
    // Equipment 类型完整性：type==Equipment 时必须 maxStack==1 且 hasEquipment 且 equipment 合法
    bool IsEquipmentValid() const;
};

} // namespace legend::item
