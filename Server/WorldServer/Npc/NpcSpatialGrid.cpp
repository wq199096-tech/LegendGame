#include "Server/WorldServer/Npc/NpcSpatialGrid.h"

#include <cmath>

namespace legend::world {

void NpcSpatialGrid::AddNpc(std::uint64_t npcEntityId, float x, float y) {
    if (m_npcCell.find(npcEntityId) != m_npcCell.end()) {
        return; // 重复 Add 忽略（NPC 静态）
    }
    const CellKey key = CellOf(x, y);
    m_cells[key].push_back(npcEntityId);
    m_npcCell[npcEntityId] = key;
}

void NpcSpatialGrid::RemoveNpc(std::uint64_t npcEntityId) {
    const auto it = m_npcCell.find(npcEntityId);
    if (it == m_npcCell.end()) {
        return;
    }
    const auto cellIt = m_cells.find(it->second);
    if (cellIt != m_cells.end()) {
        auto& ids = cellIt->second;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == npcEntityId) {
                ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
    }
    m_npcCell.erase(it);
}

std::vector<std::uint64_t> NpcSpatialGrid::QueryRange(float x, float y, float radius) const {
    std::vector<std::uint64_t> result;
    const float cell = static_cast<float>(kNpcSpatialCellSize);
    const std::int64_t minCx = static_cast<std::int64_t>((x - radius) / cell);
    const std::int64_t maxCx = static_cast<std::int64_t>((x + radius) / cell);
    const std::int64_t minCy = static_cast<std::int64_t>((y - radius) / cell);
    const std::int64_t maxCy = static_cast<std::int64_t>((y + radius) / cell);
    for (std::int64_t cx = minCx; cx <= maxCx; ++cx) {
        for (std::int64_t cy = minCy; cy <= maxCy; ++cy) {
            const auto it = m_cells.find({cx, cy});
            if (it == m_cells.end()) {
                continue;
            }
            for (const std::uint64_t npcEntityId : it->second) {
                result.push_back(npcEntityId);
            }
        }
    }
    return result;
}

} // namespace legend::world
