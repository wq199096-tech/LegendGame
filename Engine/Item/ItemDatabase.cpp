#include "Engine/Item/ItemDatabase.h"

#include <nlohmann/json.hpp>

#include <fstream>

#include "Engine/Debug/Logger.h"

namespace legend::item {

namespace {
constexpr int kSupportedVersion = 1;

bool ParseItemEntry(const nlohmann::json& entry, ItemDefinition& out) {
    if (!entry.is_object() || !entry.contains("id") || !entry.contains("name")) {
        return false;
    }
    out.id = entry["id"].get<std::string>();
    out.name = entry["name"].get<std::string>();
    out.type = ParseItemType(entry.value("type", std::string("Misc")));
    out.maxStack = entry.value("maxStack", 1);
    out.icon = entry.value("icon", std::string());
    out.description = entry.value("description", std::string());

    // ---- 阶段7：equipment 块（可选） ----
    // type==Equipment：必须带合法 equipment 块 + maxStack==1（否则该 ItemDefinition 无效）
    // 非 Equipment 带 equipment 块：Warning + 忽略（行为明确）
    if (entry.contains("equipment") && entry["equipment"].is_object()) {
        const nlohmann::json& eq = entry["equipment"];
        EquipmentData data;
        const std::string slotName = eq.value("slot", std::string());
        if (!ParseEquipmentSlotType(slotName, data.slot)) {
            LOG_ERROR("ItemDatabase: item '" + out.id + "' has unknown equipment slot '" +
                      slotName + "', entry skipped.");
            return false; // 未知 slot：Invalid（不默认 Weapon）
        }
        data.attackBonus = eq.value("attack", 0.0f);
        data.defenseBonus = eq.value("defense", 0.0f);
        data.maxHpBonus = eq.value("maxHp", 0.0f);
        if (!data.IsValid()) {
            LOG_ERROR("ItemDatabase: item '" + out.id + "' has negative equipment bonus, "
                      "entry skipped.");
            return false; // 负 bonus：非法（阶段7 无负面装备）
        }
        if (out.type == ItemType::Equipment) {
            if (out.maxStack != 1) {
                LOG_ERROR("ItemDatabase: equipment '" + out.id + "' maxStack must be 1 (got " +
                          std::to_string(out.maxStack) + "), entry skipped.");
                return false; // Equipment maxStack > 1：非法
            }
            out.hasEquipment = true;
            out.equipment = data;
        } else {
            LOG_WARN("ItemDatabase: non-equipment item '" + out.id +
                     "' has equipment block, ignored.");
        }
    } else if (out.type == ItemType::Equipment) {
        LOG_ERROR("ItemDatabase: equipment '" + out.id +
                  "' missing equipment block, entry skipped.");
        return false; // Equipment 没有 equipment 块：非法
    }
    return out.IsValid() && out.IsEquipmentValid();
}
} // namespace

bool ItemDatabase::LoadFromFile(const std::string& filePath) {
    m_items.clear();
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_ERROR("ItemDatabase: cannot open items file: " + filePath);
        return false;
    }
    nlohmann::json root;
    try {
        file >> root;
    } catch (const nlohmann::json::parse_error& error) {
        LOG_ERROR("ItemDatabase: JSON parse error in '" + filePath + "': " + error.what());
        return false;
    }
    if (!root.is_object() || !root.contains("items") || !root["items"].is_array()) {
        LOG_ERROR("ItemDatabase: file must contain 'items' array: " + filePath);
        return false;
    }
    if (root.contains("version") && root["version"].get<int>() != kSupportedVersion) {
        LOG_ERROR("ItemDatabase: unsupported version in " + filePath);
        return false;
    }

    int skipped = 0;
    for (const auto& entry : root["items"]) {
        ItemDefinition definition;
        if (!ParseItemEntry(entry, definition)) {
            ++skipped;
            continue;
        }
        if (m_items.count(definition.id) > 0) {
            LOG_WARN("ItemDatabase: duplicate id '" + definition.id + "', skipped.");
            ++skipped;
            continue;
        }
        m_items[definition.id] = std::move(definition);
    }
    if (skipped > 0) {
        LOG_WARN("ItemDatabase: skipped " + std::to_string(skipped) + " invalid entries.");
    }
    LOG_INFO("ItemDatabase loaded: " + std::to_string(m_items.size()) + " item(s) (" + filePath +
             ")");
    return !m_items.empty();
}

const ItemDefinition* ItemDatabase::Get(const std::string& id) const {
    const auto it = m_items.find(id);
    return it != m_items.end() ? &it->second : nullptr;
}

} // namespace legend::item
