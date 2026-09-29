#pragma once

#include "Shared/Item/ItemDefinition.h"

#include <cstdint>
#include <random>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段18 指令十/十一：DropRoller —— 可注入 RNG 的掉落掷骰（服务器权威）。
// 生产路径用确定性种子外的 std::mt19937；测试可注入固定 seed（CI 不因概率失败）。
// Training Slime 掉落表（指令十）：Slime Core 100% ×1 / Rusty Sword 20% /
// Cloth Armor 20%；Gold 走阶段17 Reward，不在这里。
// ---------------------------------------------------------------------------

struct DropRollResult {
    std::uint32_t definitionId = 0;
    std::uint32_t quantity = 1;
};

class DropRoller {
public:
    struct Entry {
        std::uint32_t definitionId = 0;
        double chance = 0.0; // [0,1]
        std::uint32_t minQuantity = 1;
        std::uint32_t maxQuantity = 1;
    };

    virtual ~DropRoller() = default;

    // 单次怪物掉落掷骰：返回全部命中的条目。
    virtual std::vector<DropRollResult> Roll(const std::vector<Entry>& table) = 0;

    // 阶段18 默认掉落表（Training Slime）。
    static std::vector<Entry> TrainingSlimeTable() {
        return {
            {kItemSlimeCoreId, 1.00, 1, 1},   // 指令十：100% 掉 1 个
            {kItemRustySwordId, 0.20, 1, 1},  // 20%
            {kItemClothArmorId, 0.20, 1, 1},  // 20%
        };
    }
};

// mt19937 实现（construct 时可传固定 seed 供测试复现）。
class SeededDropRoller final : public DropRoller {
public:
    explicit SeededDropRoller(std::uint64_t seed);

    std::vector<DropRollResult> Roll(const std::vector<Entry>& table) override;

private:
    std::mt19937_64 m_rng;
};

} // namespace legend::world
