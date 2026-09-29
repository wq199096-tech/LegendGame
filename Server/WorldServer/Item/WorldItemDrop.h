#pragma once

#include "Shared/Item/ItemTypes.h"

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段18 指令十二~十八：WorldItemDrop + WorldItemDropManager。
// runtime-only（不持久化，指令四十九 NoWorldDropPersistence）；
// dropEntityId 为 WorldServer 单调计数（指令十三：与 item instanceId 严格区分）。
// 仅 World io 线程访问（无锁）——拾取原子性由 single-thread authority +
// remove-before-grant 保证（指令二十三）。
// ---------------------------------------------------------------------------

struct WorldItemDrop {
    std::uint64_t dropEntityId = 0;
    std::uint32_t itemDefinitionId = 0;
    std::uint32_t quantity = 1;
    std::uint16_t mapId = 1;
    float x = 0.0f;
    float y = 0.0f;
    std::uint64_t ownerCharacterId = 0; // 0 = 无归属（公共）
    std::chrono::steady_clock::time_point ownerUntil{};  // 指令十五：击杀者独占 10s
    std::chrono::steady_clock::time_point expireAt{};    // 指令十六：TTL 60s
    bool active = true;
};

struct DropAoiCandidate {
    const WorldItemDrop* drop = nullptr;
    float distanceSquared = 0.0f;
};

struct DropAoiDelta {
    std::vector<const WorldItemDrop*> spawns;
    std::vector<std::uint64_t> despawns;
};

// 指令十八：Drop AOI（Enter 600 / Leave 700 滞回 + limit，与玩家/怪物 resolver 同构）。
DropAoiDelta ResolveDropAoiVisibility(const std::vector<DropAoiCandidate>& candidates,
                                      std::uint16_t selfMapId,
                                      const std::unordered_set<std::uint64_t>& currentVisible,
                                      float enterRadius, float leaveRadius,
                                      std::size_t visibleLimit);

class WorldItemDropManager {
public:
    explicit WorldItemDropManager(float cellSize = 200.0f);

    // 新增掉落（dropEntityId 由调用方分配；grid cell 立即生效）。
    void Add(const WorldItemDrop& drop);
    // remove-before-grant（指令二十三）：拾取前先从容器与 grid 移除并置
    // active=false；返回是否本调用方抢到（重复 claim 返回 false，防 dup）。
    bool Claim(std::uint64_t dropEntityId, WorldItemDrop& out);
    // DB 失败回滚（指令二十四：不能吞物品）——原样恢复（含原 dropEntityId）。
    void Restore(const WorldItemDrop& drop);
    // TTL 过期扫描（指令四十三：统一 500ms cleanup tick 调用）。
    std::vector<WorldItemDrop> ExpireScan(std::chrono::steady_clock::time_point now);
    // 停服清理（ServerCleanup 广播用）。
    std::vector<WorldItemDrop> RemoveAll();

    const WorldItemDrop* Find(std::uint64_t dropEntityId) const;
    std::size_t Count() const { return m_drops.size(); }
    // 指令十七：Spatial Grid 查询（禁止每 Player 每 Tick 遍历全量掉落）。
    std::vector<DropAoiCandidate> QueryNearby(float x, float y, float radius,
                                              std::uint16_t mapId) const;
    bool Contains(std::uint64_t dropEntityId) const {
        return m_drops.find(dropEntityId) != m_drops.end();
    }

private:
    struct CellCoord {
        int x = 0;
        int y = 0;
    };
    CellCoord CellOf(float x, float y) const;
    static std::uint64_t PackCell(int x, int y) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
               static_cast<std::uint32_t>(y);
    }
    void RemoveFromCell(std::uint64_t dropEntityId, std::uint64_t cell);

    float m_cellSize;
    std::unordered_map<std::uint64_t, WorldItemDrop> m_drops;
    std::unordered_map<std::uint64_t, std::uint64_t> m_dropCell; // dropEntityId -> cell
    std::unordered_map<std::uint64_t,
                       std::unordered_map<std::uint64_t, std::uint64_t>> m_cells; // cell -> set
};

} // namespace legend::world
