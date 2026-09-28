#pragma once

#include "Server/WorldServer/PlayerSession.h"

#include "Shared/World/WorldTypes.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace legend::world {

// AOI 候选（指令十六：Grid 查询 -> 过滤 mapId -> 距离比较 distanceSquared，指令八）。
struct AoiCandidate {
    std::shared_ptr<PlayerSession> player;
    float distanceSquared = 0.0f;
};

// 阶段12 指令九~十一：Uniform Grid / Spatial Hash Grid（禁止 Quadtree/Octree/RTree）。
// 职责：纯空间查询（指令六十八：地图成员归 WorldMapManager，网格只管空间索引）。
// 全部操作只在 World io 线程调用（指令六十七），内部不加锁。
class WorldSpatialGrid {
public:
    explicit WorldSpatialGrid(float cellSize = kAoiCellSize);

    void AddPlayer(const std::shared_ptr<PlayerSession>& player);
    void RemovePlayer(std::uint64_t characterId);
    // 位置变化后重挂 cell（cell 未变化则无操作，指令十二）。
    void UpdatePlayerCell(const std::shared_ptr<PlayerSession>& player);

    // 半径查询：排除自己；候选含 distanceSquared（指令八：无 sqrt）。
    // 跨 cell：扫描覆盖 radius 的周边 cell（指令十一：cellSize=400/radius=700 -> ±2 格）。
    std::vector<AoiCandidate> QueryNearbyPlayers(float x, float y, float radius,
                                                 std::uint64_t excludeCharacterId) const;

    std::size_t PlayerCount() const;
    bool Contains(std::uint64_t characterId) const;

private:
    struct CellCoord {
        int x = 0;
        int y = 0;
    };
    static std::uint64_t PackCell(int x, int y);
    CellCoord CellOf(float x, float y) const;

    float m_cellSize = kAoiCellSize;
    std::unordered_map<std::uint64_t,
                       std::unordered_map<std::uint64_t, std::shared_ptr<PlayerSession>>>
        m_cells;                                            // packedCell -> (characterId -> player)
    std::unordered_map<std::uint64_t, std::uint64_t> m_playerCell; // characterId -> packedCell
};

// ---------------------------------------------------------------------------
// 阶段12 指令十三/十四/十六/十七/二十/二十七/二十八/五十~五十二：
// AOI 可见性解析（纯逻辑，便于测试 Hysteresis/VisibleLimit/StableOrder/DifferentMap）。
// candidates 必须来自 Grid 查询（已排除自己）；当前可见集合为服务器权威 visiblePlayers。
// 返回 Spawn/Despawn 增量（不含自己；不同 mapId 绝不互相可见，指令二十）。
// ---------------------------------------------------------------------------
struct AoiDelta {
    std::vector<std::shared_ptr<PlayerSession>> spawns; // 进入可见（距离近优先，指令二十七）
    std::vector<std::uint64_t> despawns;                // 离开可见
};

AoiDelta ResolveAoiVisibility(const std::vector<AoiCandidate>& candidates,
                              std::uint16_t selfMapId,
                              const std::unordered_set<std::uint64_t>& currentVisible,
                              float enterRadius, float leaveRadius, std::size_t visibleLimit);

} // namespace legend::world
