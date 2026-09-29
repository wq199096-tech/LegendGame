#include "Server/WorldServer/Item/DropRoller.h"

#include <algorithm>

namespace legend::world {

SeededDropRoller::SeededDropRoller(std::uint64_t seed) : m_rng(seed) {}

std::vector<DropRollResult> SeededDropRoller::Roll(const std::vector<Entry>& table) {
    // 指令十一：概率判定与数量掷骰分离；同一 monster death 按表顺序消费 RNG
    //（固定 seed 下结果可复现）。
    std::vector<DropRollResult> results;
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    for (const auto& entry : table) {
        if (unit(m_rng) >= entry.chance) {
            continue;
        }
        DropRollResult result;
        result.definitionId = entry.definitionId;
        if (entry.maxQuantity > entry.minQuantity) {
            std::uniform_int_distribution<std::uint32_t> quantityDist(entry.minQuantity,
                                                                     entry.maxQuantity);
            result.quantity = quantityDist(m_rng);
        } else {
            result.quantity = entry.minQuantity;
        }
        results.push_back(result);
    }
    return results;
}

} // namespace legend::world
