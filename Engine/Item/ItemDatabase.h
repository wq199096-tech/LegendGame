#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Engine/Item/ItemDefinition.h"

namespace legend::item {

// 物品定义库：程序启动加载 items.json 一次（World/Game 级共享，
// 不允许每次掉落重新解析 JSON）。失败返回 false + 明确日志，不崩溃。
class ItemDatabase {
public:
    bool LoadFromFile(const std::string& filePath);

    const ItemDefinition* Get(const std::string& id) const; // 不存在返回 nullptr
    bool Exists(const std::string& id) const { return m_items.count(id) > 0; }
    const std::unordered_map<std::string, ItemDefinition>& GetAll() const { return m_items; }
    std::size_t Count() const { return m_items.size(); }

    // 测试专用：程序化注入定义（绕过 JSON；仅供自动测试/EquipmentChecks 使用）
    void AddTestItem(ItemDefinition definition) {
        m_items[definition.id] = std::move(definition);
    }

private:
    std::unordered_map<std::string, ItemDefinition> m_items;
};

} // namespace legend::item
