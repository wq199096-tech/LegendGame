#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段23 23.16：LootTableDefinition —— 掉落表静态定义（Data/Game/loot_tables.json）。
// 明确一种逻辑：优先使用 dropChance（独立概率判定，与阶段18 DropRoller::Roll 一致）；
// dropChance 必须 [0,1]（ValidateGameData 校验）。weight 预留不使用。
// ---------------------------------------------------------------------------
struct LootTableEntry {
    std::uint32_t itemDefinitionId = 0;
    double dropChance = 0.0;        // [0,1]
    std::uint32_t minQuantity = 1;
    std::uint32_t maxQuantity = 1;
    double weight = 0.0;            // 预留（阶段23 不使用 dropChance 之外的第二逻辑）
};

struct LootTableDefinition {
    std::uint32_t lootTableId = 0;
    std::string name;
    bool enabled = true;
    std::vector<LootTableEntry> entries;
};

} // namespace legend::world
