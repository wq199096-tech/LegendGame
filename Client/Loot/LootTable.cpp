#include "Client/Loot/LootTable.h"

#include <random>

namespace legend::world {

std::vector<LootRollResult> RollLootTable(const std::vector<LootEntry>& entries,
                                          std::mt19937& rng) {
    std::vector<LootRollResult> results;
    std::uniform_real_distribution<float> rollDist(0.0f, 1.0f);
    for (const LootEntry& entry : entries) {
        if (!entry.IsValid()) {
            continue; // 非法 entry 跳过（不崩游戏）
        }
        // [0,1)：chance=1.0 时 roll<1 恒真 -> 必掉；chance=0.0 时 roll<0 恒假 -> 永不掉
        if (rollDist(rng) >= entry.chance) {
            continue;
        }
        LootRollResult result;
        result.itemId = entry.itemId;
        std::uniform_int_distribution<int> qtyDist(entry.min, entry.max); // inclusive
        result.quantity = qtyDist(rng);
        results.push_back(std::move(result));
    }
    return results;
}

} // namespace legend::world
