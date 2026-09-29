#include "Server/WorldServer/Item/ItemRegistry.h"

namespace legend::world {

ItemRegistry::ItemRegistry() {
    // 指令三：阶段18 固定三种测试物品。
    {
        auto& def = m_definitions[0];
        def.definitionId = kItemRustySwordId;
        def.name = "Rusty Sword";
        def.type = ItemType::Weapon;
        def.maxStack = 1;
        def.attackBonus = 3;
        def.defenseBonus = 0;
        def.equipmentSlot = EquipmentSlot::Weapon;
    }
    {
        auto& def = m_definitions[1];
        def.definitionId = kItemClothArmorId;
        def.name = "Cloth Armor";
        def.type = ItemType::Armor;
        def.maxStack = 1;
        def.attackBonus = 0;
        def.defenseBonus = 2;
        def.equipmentSlot = EquipmentSlot::Armor;
    }
    {
        auto& def = m_definitions[2];
        def.definitionId = kItemSlimeCoreId;
        def.name = "Slime Core";
        def.type = ItemType::Material;
        def.maxStack = kSlimeCoreMaxStack;
        def.attackBonus = 0;
        def.defenseBonus = 0;
        def.equipmentSlot = EquipmentSlot::None;
    }
}

const ItemDefinition* ItemRegistry::Find(std::uint32_t definitionId) const {
    for (const auto& def : m_definitions) {
        if (def.definitionId == definitionId) {
            return &def;
        }
    }
    return nullptr;
}

} // namespace legend::world
