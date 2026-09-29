#pragma once

#include "Shared/Monster/LootTableDefinition.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段23 23.16/23.17：LootTableRegistry —— 掉落表注册表。
// 生产从 Data/Game/loot_tables.json 加载；Monster Definition 通过 lootTableId
// 引用；GenerateMonsterDrops 按 dropChance 独立判定（阶段18 DropRoller 语义）。
// ---------------------------------------------------------------------------
class LootTableRegistry {
public:
    static const LootTableRegistry& Instance();

    const LootTableDefinition* Find(std::uint32_t lootTableId) const;
    const std::vector<LootTableDefinition>& All() const { return m_tables; }

    static void LoadFromDefinitions(std::vector<LootTableDefinition> tables);
    static void LoadDefaults();

private:
    static LootTableRegistry& Mutable();

    std::vector<LootTableDefinition> m_tables;
};

} // namespace legend::world
