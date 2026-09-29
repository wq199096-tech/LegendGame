#include "Server/WorldServer/Portal/PortalSpatialGrid.h"

namespace legend::world {

void PortalSpatialGrid::AddPortal(std::uint64_t portalEntityId, float x, float y) {
    if (m_portalCell.find(portalEntityId) != m_portalCell.end()) {
        return; // 重复 Add 忽略（Portal 静态）
    }
    const CellKey key = CellOf(x, y);
    m_cells[key].push_back(portalEntityId);
    m_portalCell[portalEntityId] = key;
}

void PortalSpatialGrid::RemovePortal(std::uint64_t portalEntityId) {
    const auto it = m_portalCell.find(portalEntityId);
    if (it == m_portalCell.end()) {
        return;
    }
    const auto cellIt = m_cells.find(it->second);
    if (cellIt != m_cells.end()) {
        auto& ids = cellIt->second;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            if (ids[i] == portalEntityId) {
                ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
    }
    m_portalCell.erase(it);
}

std::vector<std::uint64_t> PortalSpatialGrid::QueryRange(float x, float y,
                                                         float radius) const {
    std::vector<std::uint64_t> result;
    const float cell = static_cast<float>(kAoiCellSize);
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
            for (const std::uint64_t portalEntityId : it->second) {
                result.push_back(portalEntityId);
            }
        }
    }
    return result;
}

} // namespace legend::world
