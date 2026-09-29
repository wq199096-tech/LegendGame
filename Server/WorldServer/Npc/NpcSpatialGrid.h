#pragma once

#include "Shared/Npc/NpcTypes.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令十一：NpcSpatialGrid —— NPC Uniform Grid（cellSize=400）。
// NPC 静态不移动（指令八）：Add 一次即可；支持 AOI 范围查询与 Remove（ServerCleanup）。
// 仅 io 线程访问（无锁）。
// ---------------------------------------------------------------------------
class NpcSpatialGrid {
public:
    // 添加 NPC（重复 Add 同 id 忽略）。
    void AddNpc(std::uint64_t npcEntityId, float x, float y);
    // 移除 NPC（阶段20 通常不使用，ServerCleanup 保留）。
    void RemoveNpc(std::uint64_t npcEntityId);
    // 查询 [x-radius, x+radius] 覆盖 cell 内的 NPC 候选（含精确距离过滤）。
    std::vector<std::uint64_t> QueryRange(float x, float y, float radius) const;

private:
    struct CellKey {
        std::int64_t cx;
        std::int64_t cy;
        bool operator==(const CellKey& other) const {
            return cx == other.cx && cy == other.cy;
        }
    };
    struct CellKeyHash {
        std::size_t operator()(const CellKey& key) const {
            const std::uint64_t x = static_cast<std::uint64_t>(key.cx);
            const std::uint64_t y = static_cast<std::uint64_t>(key.cy);
            return std::hash<std::uint64_t>()(x * 0x9E3779B97F4A7C15ULL ^ y);
        }
    };

    static CellKey CellOf(float x, float y) {
        return {static_cast<std::int64_t>(x / static_cast<float>(kNpcSpatialCellSize)),
                static_cast<std::int64_t>(y / static_cast<float>(kNpcSpatialCellSize))};
    }

    std::unordered_map<CellKey, std::vector<std::uint64_t>, CellKeyHash> m_cells;
    std::unordered_map<std::uint64_t, CellKey> m_npcCell;
};

} // namespace legend::world
