#pragma once

#include "Shared/Item/ItemDefinition.h"

#include <cstdint>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段18：ItemRegistry —— 物品定义注册表。
// 阶段23 23.22：生产从 Data/Game/items.json 加载（WorldServer::Start 注入）；
// 目录缺失时用 MakeDefaultGameData 的出厂定义。World io 线程与测试线程并发读安全。
// ---------------------------------------------------------------------------
class ItemRegistry {
public:
    ItemRegistry();

    std::size_t Count() const { return m_definitions.size(); }
    const ItemDefinition* Find(std::uint32_t definitionId) const;
    const ItemDefinition* At(std::size_t index) const {
        return index < m_definitions.size() ? &m_definitions[index] : nullptr;
    }

    // 数据注入（WorldServer::Start）。
    void LoadFromDefinitions(std::vector<ItemDefinition> definitions);
    void LoadDefaults();

private:
    std::vector<ItemDefinition> m_definitions;
};

} // namespace legend::world
