#pragma once

#include "Shared/Portal/PortalTypes.h"
#include "Shared/World/WorldTypes.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段21 指令十七：PortalSpatialGrid —— Portal Uniform Grid（cellSize=400，
// 同 NpcSpatialGrid 模式）。Portal 静态不移动：Add 一次即可。仅 io 线程访问。
// ---------------------------------------------------------------------------
class PortalSpatialGrid {
public:
    // 添加 Portal（重复 Add 同 id 忽略）。
    void AddPortal(std::uint64_t portalEntityId, float x, float y);
    // 移除 Portal（ServerCleanup 保留）。
    void RemovePortal(std::uint64_t portalEntityId);
    // 查询 [x-radius, x+radius] 覆盖 cell 内的 Portal 候选（精确距离由调用方过滤）。
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
        return {static_cast<std::int64_t>(x / static_cast<float>(kAoiCellSize)),
                static_cast<std::int64_t>(y / static_cast<float>(kAoiCellSize))};
    }

    std::unordered_map<CellKey, std::vector<std::uint64_t>, CellKeyHash> m_cells;
    std::unordered_map<std::uint64_t, CellKey> m_portalCell;
};

} // namespace legend::world
