#pragma once

#include <cstdint>
#include <string>

namespace legend::item {

// 物品类型：阶段6 只做数据与背包，不做装备/强化逻辑（Quest 类型预留）
enum class ItemType : uint8_t {
    Consumable = 0,
    Material = 1,
    Quest = 2,
    Misc = 3,
};

const char* ItemTypeName(ItemType type);
ItemType ParseItemType(const std::string& name, ItemType fallback = ItemType::Misc);

// 物品定义（Assets/Items/items.json）：数据驱动，启动加载一次
struct ItemDefinition {
    std::string id;         // 如 "small_potion"
    std::string name;       // 显示名，如 "Small Potion"
    ItemType type = ItemType::Misc;
    int maxStack = 1;       // 1 = 不可堆叠（未来 Equipment maxStack=1 自动支持）
    std::string icon;       // 纹理路径（阶段6 用 Debug 色块，不加载正式美术）
    std::string description;

    // id/name 非空 + maxStack >= 1
    bool IsValid() const;
};

} // namespace legend::item
