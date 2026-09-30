#include "Client/Ui/ItemDisplayCatalog.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace legend::ui {

namespace {

namespace fs = std::filesystem;

using nlohmann::json;

bool ReadJsonFile(const std::string& filePath, json& out, std::string& error) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        error = filePath + ": file not found";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    out = json::parse(text, nullptr, false);
    if (out.is_discarded()) {
        error = filePath + ": invalid JSON syntax";
        return false;
    }
    if (!out.is_object()) {
        error = filePath + ": top-level must be an object";
        return false;
    }
    return true;
}

std::string ReadStringField(const json& obj, const char* key, const std::string& fallback) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        return fallback;
    }
    return it->get<std::string>();
}

std::uint32_t ReadUintField(const json& obj, const char* key, std::uint32_t fallback) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_unsigned()) {
        return fallback;
    }
    return it->get<std::uint32_t>();
}

} // namespace

bool ItemDisplayCatalog::Load(const std::string& dataRoot, std::string& error) {
    const fs::path path = fs::path(dataRoot) / "Game" / "items.json";
    json root;
    if (!ReadJsonFile(path.string(), root, error)) {
        return false;
    }
    const auto itemsIt = root.find("items");
    if (itemsIt == root.end() || !itemsIt->is_array()) {
        error = "items.json: missing/invalid array 'items'";
        return false;
    }
    m_items.clear();
    for (const auto& e : *itemsIt) {
        ItemDisplay item;
        const auto idIt = e.find("itemDefinitionId");
        if (idIt == e.end() || !idIt->is_number_unsigned()) {
            error = "items.json: entry missing itemDefinitionId";
            return false;
        }
        item.definitionId = idIt->get<std::uint32_t>();
        item.name = ReadStringField(e, "name", "");
        item.type = ReadStringField(e, "type", "");
        item.equipSlot = ReadStringField(e, "equipSlot", "None");
        item.iconKey = ReadStringField(e, "iconKey", "");
        item.attackBonus = ReadUintField(e, "attackBonus", 0);
        item.defenseBonus = ReadUintField(e, "defenseBonus", 0);
        item.maxStack = ReadUintField(e, "maxStack", 1);
        m_items[item.definitionId] = std::move(item);
    }
    return true;
}

const ItemDisplay* ItemDisplayCatalog::Find(std::uint32_t definitionId) const {
    const auto it = m_items.find(definitionId);
    return it != m_items.end() ? &it->second : nullptr;
}

std::string ItemDisplayCatalog::IconAsset(std::uint32_t definitionId) const {
    const auto it = m_items.find(definitionId);
    return it != m_items.end() ? it->second.iconKey : std::string();
}

std::string ItemDisplayCatalog::DisplayName(std::uint32_t definitionId) const {
    const auto it = m_items.find(definitionId);
    return it != m_items.end() ? it->second.name : std::string();
}

} // namespace legend::ui
