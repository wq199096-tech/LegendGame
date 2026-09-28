#include "Server/WorldServer/Monster/MonsterSpatialGrid.h"

#include <algorithm>
#include <cmath>

namespace legend::world {

MonsterSpatialGrid::MonsterSpatialGrid(float cellSize)
    : m_cellSize(cellSize > 0.0f ? cellSize : kAoiCellSize) {}

std::uint64_t MonsterSpatialGrid::PackCell(int x, int y) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
           static_cast<std::uint32_t>(y);
}

MonsterSpatialGrid::CellCoord MonsterSpatialGrid::CellOf(float x, float y) const {
    CellCoord cell;
    cell.x = static_cast<int>(std::floor(x / m_cellSize));
    cell.y = static_cast<int>(std::floor(y / m_cellSize));
    return cell;
}

void MonsterSpatialGrid::AddMonster(const std::shared_ptr<MonsterEntity>& monster) {
    if (!monster) {
        return;
    }
    const CellCoord cellCoord = CellOf(monster->PositionX(), monster->PositionY());
    const std::uint64_t cell = PackCell(cellCoord.x, cellCoord.y);
    m_cells[cell][monster->EntityId()] = monster;
    m_monsterCell[monster->EntityId()] = cell;
}

void MonsterSpatialGrid::RemoveMonster(std::uint64_t entityId) {
    const auto cellIt = m_monsterCell.find(entityId);
    if (cellIt == m_monsterCell.end()) {
        return;
    }
    const auto cellsIt = m_cells.find(cellIt->second);
    if (cellsIt != m_cells.end()) {
        cellsIt->second.erase(entityId);
        if (cellsIt->second.empty()) {
            m_cells.erase(cellsIt);
        }
    }
    m_monsterCell.erase(cellIt);
}

void MonsterSpatialGrid::UpdateMonsterCell(const std::shared_ptr<MonsterEntity>& monster) {
    if (!monster) {
        return;
    }
    const CellCoord cellCoord = CellOf(monster->PositionX(), monster->PositionY());
    const std::uint64_t newCell = PackCell(cellCoord.x, cellCoord.y);
    const auto cellIt = m_monsterCell.find(monster->EntityId());
    if (cellIt == m_monsterCell.end()) {
        AddMonster(monster);
        return;
    }
    if (cellIt->second == newCell) {
        return; // cell 未变：无操作
    }
    const auto cellsIt = m_cells.find(cellIt->second);
    if (cellsIt != m_cells.end()) {
        cellsIt->second.erase(monster->EntityId());
        if (cellsIt->second.empty()) {
            m_cells.erase(cellsIt);
        }
    }
    cellIt->second = newCell;
    m_cells[newCell][monster->EntityId()] = monster;
}

std::vector<MonsterAoiCandidate> MonsterSpatialGrid::QueryNearbyMonsters(
    float x, float y, float radius, std::uint64_t excludeEntityId) const {
    std::vector<MonsterAoiCandidate> result;
    const CellCoord center = CellOf(x, y);
    const int range = static_cast<int>(std::ceil(radius / m_cellSize)); // 跨 cell 覆盖半径
    const float radiusSq = radius * radius;
    result.reserve(16);
    for (int dy = -range; dy <= range; ++dy) {
        for (int dx = -range; dx <= range; ++dx) {
            const auto cellIt = m_cells.find(PackCell(center.x + dx, center.y + dy));
            if (cellIt == m_cells.end()) {
                continue;
            }
            for (const auto& [entityId, monster] : cellIt->second) {
                if (entityId == excludeEntityId) {
                    continue;
                }
                const float deltaX = monster->PositionX() - x;
                const float deltaY = monster->PositionY() - y;
                const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
                if (distanceSquared <= radiusSq) {
                    result.push_back({monster, distanceSquared});
                }
            }
        }
    }
    return result;
}

std::size_t MonsterSpatialGrid::MonsterCount() const {
    return m_monsterCell.size();
}

bool MonsterSpatialGrid::Contains(std::uint64_t entityId) const {
    return m_monsterCell.find(entityId) != m_monsterCell.end();
}

// ---------------------------------------------------------------------------
// 怪物可见性解析（与 ResolveAoiVisibility 同构：滞回 + limit + 稳定排序）
// ---------------------------------------------------------------------------

MonsterAoiDelta ResolveMonsterAoiVisibility(
    const std::vector<MonsterAoiCandidate>& candidates, std::uint16_t selfMapId,
    const std::unordered_set<std::uint64_t>& currentVisible, float enterRadius, float leaveRadius,
    std::size_t visibleLimit) {
    MonsterAoiDelta delta;
    const float enterRadiusSq = enterRadius * enterRadius;
    const float leaveRadiusSq = leaveRadius * leaveRadius;

    // 距离升序 -> entityId 升序（稳定，与玩家 resolver 的排序语义一致）。
    std::vector<const MonsterAoiCandidate*> sorted;
    sorted.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (candidate.monster && candidate.monster->MapId() == selfMapId) {
            sorted.push_back(&candidate);
        }
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const MonsterAoiCandidate* a, const MonsterAoiCandidate* b) {
                  if (a->distanceSquared != b->distanceSquared) {
                      return a->distanceSquared < b->distanceSquared;
                  }
                  return a->monster->EntityId() < b->monster->EntityId();
              });

    // 保留：已可见 && distSq <= leaveRadiusSq（滞回）。
    std::size_t keptVisible = 0;
    for (const MonsterAoiCandidate* candidate : sorted) {
        const std::uint64_t id = candidate->monster->EntityId();
        if (currentVisible.count(id) != 0 && candidate->distanceSquared <= leaveRadiusSq) {
            ++keptVisible;
        }
    }
    // Despawn：已可见但（>leaveRadius 或不在候选 = 查询半径外）。
    for (const auto id : currentVisible) {
        bool kept = false;
        for (const MonsterAoiCandidate* candidate : sorted) {
            if (candidate->monster->EntityId() == id) {
                kept = candidate->distanceSquared <= leaveRadiusSq;
                break;
            }
        }
        if (!kept) {
            delta.despawns.push_back(id);
        }
    }

    // Spawn：未可见 && distSq <= enterRadiusSq；limit 内近优先。
    std::size_t capacity = visibleLimit > keptVisible ? visibleLimit - keptVisible : 0;
    for (const MonsterAoiCandidate* candidate : sorted) {
        if (capacity == 0) {
            break;
        }
        const std::uint64_t id = candidate->monster->EntityId();
        if (currentVisible.count(id) == 0 && candidate->distanceSquared <= enterRadiusSq) {
            delta.spawns.push_back(candidate->monster);
            --capacity;
        }
    }
    return delta;
}

} // namespace legend::world
