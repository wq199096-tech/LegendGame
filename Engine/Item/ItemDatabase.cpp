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
    return out.IsValid();
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
