#include "Server/WorldServer/Item/WorldItemDrop.h"

#include <algorithm>
#include <cmath>

namespace legend::world {

DropAoiDelta ResolveDropAoiVisibility(const std::vector<DropAoiCandidate>& candidates,
                                      std::uint16_t selfMapId,
                                      const std::unordered_set<std::uint64_t>& currentVisible,
                                      float enterRadius, float leaveRadius,
                                      std::size_t visibleLimit) {
    DropAoiDelta delta;
    const float enterRadiusSq = enterRadius * enterRadius;
    const float leaveRadiusSq = leaveRadius * leaveRadius;

    // 距离升序 -> dropEntityId 升序（稳定，与其他 resolver 语义一致）。
    std::vector<const DropAoiCandidate*> sorted;
    sorted.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (candidate.drop && candidate.drop->mapId == selfMapId) {
            sorted.push_back(&candidate);
        }
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const DropAoiCandidate* a, const DropAoiCandidate* b) {
                  if (a->distanceSquared != b->distanceSquared) {
                      return a->distanceSquared < b->distanceSquared;
                  }
                  return a->drop->dropEntityId < b->drop->dropEntityId;
              });

    // 保留计数（滞回：已可见 && distSq <= leaveRadiusSq）。
    std::size_t keptVisible = 0;
    for (const DropAoiCandidate* candidate : sorted) {
        const std::uint64_t id = candidate->drop->dropEntityId;
        if (currentVisible.count(id) != 0 && candidate->distanceSquared <= leaveRadiusSq) {
            ++keptVisible;
        }
    }
    // Despawn：已可见但 >leaveRadius 或不在候选（查询半径外/已被拾取移除）。
    for (const auto id : currentVisible) {
        bool kept = false;
        for (const DropAoiCandidate* candidate : sorted) {
            if (candidate->drop->dropEntityId == id) {
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
    for (const DropAoiCandidate* candidate : sorted) {
        if (capacity == 0) {
            break;
        }
        const std::uint64_t id = candidate->drop->dropEntityId;
        if (currentVisible.count(id) == 0 && candidate->distanceSquared <= enterRadiusSq) {
            delta.spawns.push_back(candidate->drop);
            --capacity;
        }
    }
    return delta;
}

WorldItemDropManager::WorldItemDropManager(float cellSize)
    : m_cellSize(cellSize > 0.0f ? cellSize : 200.0f) {}

WorldItemDropManager::CellCoord WorldItemDropManager::CellOf(float x, float y) const {
    CellCoord cell;
    cell.x = static_cast<int>(std::floor(x / m_cellSize));
    cell.y = static_cast<int>(std::floor(y / m_cellSize));
    return cell;
}

void WorldItemDropManager::Add(const WorldItemDrop& drop) {
    if (!drop.active) {
        return;
    }
    const CellCoord cellCoord = CellOf(drop.x, drop.y);
    const std::uint64_t cell = PackCell(cellCoord.x, cellCoord.y);
    m_drops[drop.dropEntityId] = drop;
    m_dropCell[drop.dropEntityId] = cell;
    m_cells[cell][drop.dropEntityId] = drop.dropEntityId;
}

bool WorldItemDropManager::Claim(std::uint64_t dropEntityId, WorldItemDrop& out) {
    const auto it = m_drops.find(dropEntityId);
    if (it == m_drops.end() || !it->second.active) {
        return false; // 已被抢走/不存在（指令二十三：成功只能有一个）
    }
    out = it->second;
    out.active = false;
    RemoveFromCell(dropEntityId, m_dropCell[out.dropEntityId]);
    m_dropCell.erase(out.dropEntityId);
    m_drops.erase(it);
    return true;
}

void WorldItemDropManager::Restore(const WorldItemDrop& drop) {
    WorldItemDrop restored = drop;
    restored.active = true;
    Add(restored);
}

std::vector<WorldItemDrop> WorldItemDropManager::ExpireScan(
    std::chrono::steady_clock::time_point now) {
    std::vector<std::uint64_t> expiredIds;
    for (const auto& [id, drop] : m_drops) {
        if (now >= drop.expireAt) {
            expiredIds.push_back(id);
        }
    }
    std::vector<WorldItemDrop> expired;
    expired.reserve(expiredIds.size());
    for (const auto id : expiredIds) {
        const auto it = m_drops.find(id);
        if (it == m_drops.end()) {
            continue;
        }
        expired.push_back(it->second);
        RemoveFromCell(id, m_dropCell[id]);
        m_dropCell.erase(id);
        m_drops.erase(it);
    }
    return expired;
}

std::vector<WorldItemDrop> WorldItemDropManager::RemoveAll() {
    std::vector<WorldItemDrop> all;
    all.reserve(m_drops.size());
    for (const auto& [id, drop] : m_drops) {
        all.push_back(drop);
    }
    m_drops.clear();
    m_dropCell.clear();
    m_cells.clear();
    return all;
}

const WorldItemDrop* WorldItemDropManager::Find(std::uint64_t dropEntityId) const {
    const auto it = m_drops.find(dropEntityId);
    return it != m_drops.end() ? &it->second : nullptr;
}

std::vector<DropAoiCandidate> WorldItemDropManager::QueryNearby(float x, float y, float radius,
                                                                std::uint16_t mapId) const {
    std::vector<DropAoiCandidate> result;
    const CellCoord center = CellOf(x, y);
    const int range = static_cast<int>(std::ceil(radius / m_cellSize));
    const float radiusSq = radius * radius;
    result.reserve(16);
    for (int dy = -range; dy <= range; ++dy) {
        for (int dx = -range; dx <= range; ++dx) {
            const auto cellIt = m_cells.find(PackCell(center.x + dx, center.y + dy));
            if (cellIt == m_cells.end()) {
                continue;
            }
            for (const auto& [entityId, dropId] : cellIt->second) {
                const auto dropIt = m_drops.find(dropId);
                if (dropIt == m_drops.end() || dropIt->second.mapId != mapId) {
                    continue;
                }
                const float deltaX = dropIt->second.x - x;
                const float deltaY = dropIt->second.y - y;
                const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
                if (distanceSquared <= radiusSq) {
                    result.push_back({&dropIt->second, distanceSquared});
                }
            }
        }
    }
    return result;
}

void WorldItemDropManager::RemoveFromCell(std::uint64_t dropEntityId, std::uint64_t cell) {
    const auto cellsIt = m_cells.find(cell);
    if (cellsIt == m_cells.end()) {
        return;
    }
    cellsIt->second.erase(dropEntityId);
    if (cellsIt->second.empty()) {
        m_cells.erase(cellsIt);
    }
}

} // namespace legend::world
