#include "Server/WorldServer/AOI/WorldSpatialGrid.h"

#include <algorithm>
#include <cmath>

namespace legend::world {

WorldSpatialGrid::WorldSpatialGrid(float cellSize) : m_cellSize(cellSize > 0.0f ? cellSize : kAoiCellSize) {}

std::uint64_t WorldSpatialGrid::PackCell(int x, int y) {
    // 负坐标安全打包（位置已 Sanitize 到 0~2000，负值仅防御）。
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
           static_cast<std::uint32_t>(y);
}

WorldSpatialGrid::CellCoord WorldSpatialGrid::CellOf(float x, float y) const {
    CellCoord cell;
    cell.x = static_cast<int>(std::floor(x / m_cellSize));
    cell.y = static_cast<int>(std::floor(y / m_cellSize));
    return cell;
}

void WorldSpatialGrid::AddPlayer(const std::shared_ptr<PlayerSession>& player) {
    if (!player) {
        return;
    }
    const CellCoord cellCoord = CellOf(player->PositionX(), player->PositionY());
    const std::uint64_t cell = PackCell(cellCoord.x, cellCoord.y);
    m_cells[cell][player->CharacterId()] = player;
    m_playerCell[player->CharacterId()] = cell;
}

void WorldSpatialGrid::RemovePlayer(std::uint64_t characterId) {
    const auto cellIt = m_playerCell.find(characterId);
    if (cellIt == m_playerCell.end()) {
        return;
    }
    const auto cellsIt = m_cells.find(cellIt->second);
    if (cellsIt != m_cells.end()) {
        cellsIt->second.erase(characterId);
        if (cellsIt->second.empty()) {
            m_cells.erase(cellsIt);
        }
    }
    m_playerCell.erase(cellIt);
}

void WorldSpatialGrid::UpdatePlayerCell(const std::shared_ptr<PlayerSession>& player) {
    if (!player) {
        return;
    }
    const CellCoord cellCoord = CellOf(player->PositionX(), player->PositionY());
    const std::uint64_t newCell = PackCell(cellCoord.x, cellCoord.y);
    const auto cellIt = m_playerCell.find(player->CharacterId());
    if (cellIt == m_playerCell.end()) {
        AddPlayer(player);
        return;
    }
    if (cellIt->second == newCell) {
        return; // cell 未变：无操作（指令十二）
    }
    const auto cellsIt = m_cells.find(cellIt->second);
    if (cellsIt != m_cells.end()) {
        cellsIt->second.erase(player->CharacterId());
        if (cellsIt->second.empty()) {
            m_cells.erase(cellsIt);
        }
    }
    cellIt->second = newCell;
    m_cells[newCell][player->CharacterId()] = player;
}

std::vector<AoiCandidate> WorldSpatialGrid::QueryNearbyPlayers(float x, float y, float radius,
                                                               std::uint64_t excludeCharacterId) const {
    std::vector<AoiCandidate> result;
    const CellCoord center = CellOf(x, y);
    const int range = static_cast<int>(std::ceil(radius / m_cellSize)); // 指令十一：覆盖半径
    const float radiusSq = radius * radius;
    result.reserve(16);
    for (int dy = -range; dy <= range; ++dy) {
        for (int dx = -range; dx <= range; ++dx) {
            const auto cellIt = m_cells.find(PackCell(center.x + dx, center.y + dy));
            if (cellIt == m_cells.end()) {
                continue;
            }
            for (const auto& [characterId, player] : cellIt->second) {
                if (characterId == excludeCharacterId) {
                    continue;
                }
                const float deltaX = player->PositionX() - x;
                const float deltaY = player->PositionY() - y;
                // 指令八：只比较 distanceSquared，不做 sqrt。
                const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
                if (distanceSquared <= radiusSq) {
                    result.push_back({player, distanceSquared});
                }
            }
        }
    }
    return result;
}

std::size_t WorldSpatialGrid::PlayerCount() const {
    return m_playerCell.size();
}

bool WorldSpatialGrid::Contains(std::uint64_t characterId) const {
    return m_playerCell.find(characterId) != m_playerCell.end();
}

// ---------------------------------------------------------------------------
// AOI 可见性解析
// ---------------------------------------------------------------------------

AoiDelta ResolveAoiVisibility(const std::vector<AoiCandidate>& candidates,
                              std::uint16_t selfMapId,
                              const std::unordered_set<std::uint64_t>& currentVisible,
                              float enterRadius, float leaveRadius, std::size_t visibleLimit) {
    AoiDelta delta;
    const float enterRadiusSq = enterRadius * enterRadius;
    const float leaveRadiusSq = leaveRadius * leaveRadius;

    // 指令二十/二十八：过滤不同 mapId -> 距离升序 -> characterId 升序（稳定排序）。
    std::vector<const AoiCandidate*> sorted;
    sorted.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (candidate.player && candidate.player->MapId() == selfMapId) {
            sorted.push_back(&candidate);
        }
    }
    std::sort(sorted.begin(), sorted.end(), [](const AoiCandidate* a, const AoiCandidate* b) {
        if (a->distanceSquared != b->distanceSquared) {
            return a->distanceSquared < b->distanceSquared;
        }
        return a->player->CharacterId() < b->player->CharacterId();
    });

    // 保留：已可见 && distSq <= leaveRadiusSq（指令七：滞回，>700 才离开）。
    std::size_t keptVisible = 0;
    for (const AoiCandidate* candidate : sorted) {
        const std::uint64_t id = candidate->player->CharacterId();
        if (currentVisible.count(id) != 0 && candidate->distanceSquared <= leaveRadiusSq) {
            ++keptVisible;
        }
    }
    // Despawn：已可见但（距离 > leaveRadius 或不在候选 = 查询半径外）。
    for (const auto id : currentVisible) {
        bool kept = false;
        for (const AoiCandidate* candidate : sorted) {
            if (candidate->player->CharacterId() == id) {
                kept = candidate->distanceSquared <= leaveRadiusSq;
                break;
            }
        }
        if (!kept) {
            delta.despawns.push_back(id);
        }
    }

    // Spawn：未可见 && distSq <= enterRadiusSq；visibleLimit 上限内按距离近优先
    //（指令二十七/五十：capacity = limit - 保留数）。
    std::size_t capacity = visibleLimit > keptVisible ? visibleLimit - keptVisible : 0;
    for (const AoiCandidate* candidate : sorted) {
        if (capacity == 0) {
            break;
        }
        const std::uint64_t id = candidate->player->CharacterId();
        if (currentVisible.count(id) == 0 && candidate->distanceSquared <= enterRadiusSq) {
            delta.spawns.push_back(candidate->player);
            --capacity;
        }
    }
    return delta;
}

} // namespace legend::world
