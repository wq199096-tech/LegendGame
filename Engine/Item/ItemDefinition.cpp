#include "Engine/Item/ItemDefinition.h"

#include <unordered_map>

namespace legend::item {

namespace {
const std::unordered_map<std::string, ItemType> kTypeNames = {
    {"Consumable", ItemType::Consumable},
    {"Material", ItemType::Material},
    {"Quest", ItemType::Quest},
    {"Misc", ItemType::Misc},
};
} // namespace

const char* ItemTypeName(ItemType type) {
    switch (type) {
    case ItemType::Consumable: return "Consumable";
    case ItemType::Material: return "Material";
    case ItemType::Quest: return "Quest";
    case ItemType::Misc: break;
    }
    return "Misc";
}

ItemType ParseItemType(const std::string& name, ItemType fallback) {
    const auto it = kTypeNames.find(name);
    return it != kTypeNames.end() ? it->second : fallback;
}

bool ItemDefinition::IsValid() const {
    return !id.empty() && !name.empty() && maxStack >= 1;
}

} // namespace legend::item
