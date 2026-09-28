#pragma once

#include "Server/WorldServer/Monster/MonsterEntity.h"

#include "Shared/Monster/MonsterTypes.h"
#include "Shared/World/WorldTypes.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace legend::world {

// 怪物 AOI 候选（指令三十五：grid 查询 -> 过滤 mapId -> distanceSquared 比较）。
struct MonsterAoiCandidate {
    std::shared_ptr<MonsterEntity> monster;
    float distanceSquared = 0.0f;
};

// 阶段13 指令十一/十二：MonsterSpatialGrid —— 怪物空间索引（不塞 PlayerSession）。
// cellSize=400（指令十二）；仅 World io 线程访问（指令三十一），内部不加锁。
class MonsterSpatialGrid {
public:
    explicit MonsterSpatialGrid(float cellSize = kAoiCellSize);

    void AddMonster(const std::shared_ptr<MonsterEntity>& monster);
    void RemoveMonster(std::uint64_t entityId);
    // AI 移动后重挂 cell（cell 未变化则无操作）。
    void UpdateMonsterCell(const std::shared_ptr<MonsterEntity>& monster);

    // 半径查询：excludeEntityId 排除自己；候选含 distanceSquared。
    std::vector<MonsterAoiCandidate> QueryNearbyMonsters(float x, float y, float radius,
                                                         std::uint64_t excludeEntityId) const;

    std::size_t MonsterCount() const;
    bool Contains(std::uint64_t entityId) const;

private:
    struct CellCoord {
        int x = 0;
        int y = 0;
    };
    static std::uint64_t PackCell(int x, int y);
    CellCoord CellOf(float x, float y) const;

    float m_cellSize = kAoiCellSize;
    std::unordered_map<std::uint64_t,
                       std::unordered_map<std::uint64_t, std::shared_ptr<MonsterEntity>>>
        m_cells;                                              // packedCell -> (entityId -> monster)
    std::unordered_map<std::uint64_t, std::uint64_t> m_monsterCell; // entityId -> packedCell
};

// 阶段13 指令二十五~二十九：怪物可见性解析（纯逻辑；与玩家 resolver 同构，
// 按 entityId 工作，供 Monster AOI 差量与测试复用）。
// candidates 必须来自 grid 查询；不同 mapId 绝不互相可见。
struct MonsterAoiDelta {
    std::vector<std::shared_ptr<MonsterEntity>> spawns; // 进入可见（距离近优先）
    std::vector<std::uint64_t> despawns;                // 离开可见
};

MonsterAoiDelta ResolveMonsterAoiVisibility(
    const std::vector<MonsterAoiCandidate>& candidates, std::uint16_t selfMapId,
    const std::unordered_set<std::uint64_t>& currentVisible, float enterRadius, float leaveRadius,
    std::size_t visibleLimit);

} // namespace legend::world
