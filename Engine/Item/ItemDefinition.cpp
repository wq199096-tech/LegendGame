#include "Engine/Item/ItemDefinition.h"

#include <unordered_map>

namespace legend::item {

namespace {
const std::unordered_map<std::string, ItemType> kTypeNames = {
    {"Consumable", ItemType::Consumable},
    {"Material", ItemType::Material},
    {"Quest", ItemType::Quest},
    {"Misc", ItemType::Misc},
    {"Equipment", ItemType::Equipment},
};

const std::unordered_map<std::string, EquipmentSlotType> kSlotNames = {
    {"Weapon", EquipmentSlotType::Weapon},
    {"Helmet", EquipmentSlotType::Helmet},
    {"Armor", EquipmentSlotType::Armor},
    {"Necklace", EquipmentSlotType::Necklace},
    {"Ring", EquipmentSlotType::Ring},
    {"Boots", EquipmentSlotType::Boots},
};
} // namespace

const char* ItemTypeName(ItemType type) {
    switch (type) {
    case ItemType::Consumable: return "Consumable";
    case ItemType::Material: return "Material";
    case ItemType::Quest: return "Quest";
    case ItemType::Equipment: return "Equipment";
    case ItemType::Misc: break;
    }
    return "Misc";
}

ItemType ParseItemType(const std::string& name, ItemType fallback) {
    const auto it = kTypeNames.find(name);
    return it != kTypeNames.end() ? it->second : fallback;
}

const char* EquipmentSlotTypeName(EquipmentSlotType slot) {
    switch (slot) {
    case EquipmentSlotType::Weapon: return "Weapon";
    case EquipmentSlotType::Helmet: return "Helmet";
    case EquipmentSlotType::Armor: return "Armor";
    case EquipmentSlotType::Necklace: return "Necklace";
    case EquipmentSlotType::Ring: return "Ring";
    case EquipmentSlotType::Boots: break;
    }
    return "Boots";
}

bool ParseEquipmentSlotType(const std::string& name, EquipmentSlotType& out) {
    // 未知字符串返回 false（不默认 Weapon——阶段7 指令六十五）
    const auto it = kSlotNames.find(name);
    if (it == kSlotNames.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool EquipmentData::IsValid() const {
    return attackBonus >= 0.0f && defenseBonus >= 0.0f && maxHpBonus >= 0.0f;
}

bool ItemDefinition::IsValid() const {
    return !id.empty() && !name.empty() && maxStack >= 1;
}

bool ItemDefinition::IsEquipmentValid() const {
    // Equipment 类型完整性（阶段7 指令七/八）：
    // maxStack 必须 == 1、必须带合法 equipment 块（slot 由 Parse 保证 + bonus 非负）
    if (type != ItemType::Equipment) {
        return true; // 非 Equipment 类型无此约束
    }
    return maxStack == 1 && hasEquipment && equipment.IsValid();
}

} // namespace legend::item
