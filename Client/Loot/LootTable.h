#pragma once

#include <random>
#include <string>
#include <vector>

namespace legend::world {

// monster.json "loot" 数组条目：数据驱动掉落（死亡时从内存表 Roll，不重读 JSON）
struct LootEntry {
    std::string itemId;
    float chance = 0.0f; // 0.0 ~ 1.0（1 必掉 / 0 永不掉）
    int min = 1;         // 数量下限（inclusive，>=1）
    int max = 1;         // 数量上限（inclusive，>=min）

    // 数值合法性（item 存在性校验需要 ItemDatabase，在加载后统一做）
    bool IsValid() const {
        return !itemId.empty() && chance >= 0.0f && chance <= 1.0f && min >= 1 && max >= min;
    }
};

// 单次掷出的掉落结果
struct LootRollResult {
    std::string itemId;
    int quantity = 0;
};

// 掷掉落表：uniform_real_distribution(0,1)（禁 rand()%100）；chance=1 必掉、=0 永不掉；
// 数量 uniform_int_distribution [min,max]（inclusive）。非法 entry 跳过。
std::vector<LootRollResult> RollLootTable(const std::vector<LootEntry>& entries,
                                          std::mt19937& rng);

} // namespace legend::world
