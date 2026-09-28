#pragma once

#include "Shared/Monster/MonsterTypes.h"

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段17 指令十九/二十：MonsterSpawnSlot —— 固定出生点槽位（runtime 配置）。
// 不能简单复活旧 entity（指令二十一）：Respawn 总是创建新的 MonsterEntity 并
// 分配新 entityId；slot 只描述"哪里、什么怪、多久重生"。
// ---------------------------------------------------------------------------
struct MonsterSpawnSlot {
    std::uint32_t spawnSlotId = 0;      // 指令二十：1~20
    std::uint32_t monsterTypeId = 0;
    std::uint16_t mapId = 1;
    float spawnX = 0.0f;
    float spawnY = 0.0f;
    std::uint32_t respawnDelayMs = 0;   // 指令二十二：Training Slime = 8000
};

// ---------------------------------------------------------------------------
// 阶段17 指令二十三/二十四/二十五：MonsterRespawnManager —— 重生队列管理。
// 统一 Respawn Tick 250ms 轮询（不建每怪 Timer）；
// respawnTime 从 MonsterDeath 时刻起算（尸体 3 秒包含在 8 秒内，指令二十二）。
// 只有 slot 当前没有 active entity 且 respawnTime 到达才允许 Spawn（防重复复活）。
// runtime-only：World 重启队列清空、20 slot 全部重新满怪（指令二十九）。
// 仅 World io 线程访问（无锁）。
// ---------------------------------------------------------------------------
class MonsterRespawnManager {
public:
    // 指令二十六：初始化 20 个固定 SpawnSlot（kInitialMonsterSpawnTable 顺序即 slotId）。
    void InitializeFromTable(const MonsterSpawnPoint (&table)[kInitialMonsterCount],
                             std::uint32_t monsterTypeId, std::uint16_t mapId,
                             std::uint32_t respawnDelayMs);

    const std::vector<MonsterSpawnSlot>& Slots() const { return m_slots; }
    const MonsterSpawnSlot* FindSlot(std::uint32_t spawnSlotId) const;

    // 指令二十三：activeEntityBySlot 维护（初始 Spawn 与 Respawn 时注册）。
    void BindEntity(std::uint32_t spawnSlotId, std::uint64_t entityId);
    // 实体从世界移除（尸体 3 秒清理）-> 解除 slot 绑定并进入 Respawn 队列。
    // respawnTime = deathTime + respawnDelayMs（指令二十二：从 MonsterDeath 起算）。
    void NotifyEntityRemoved(std::uint64_t entityId,
                             std::chrono::steady_clock::time_point deathTime);

    // 指令二十四：Respawn Tick 轮询 —— 返回本轮到点、可以 Spawn 的 slot 列表。
    struct RespawnDue {
        std::uint32_t spawnSlotId = 0;
        MonsterSpawnSlot slot;
    };
    std::vector<RespawnDue> Poll(std::chrono::steady_clock::time_point now);
    // Spawn 成功后绑定新实体（指令二十一：新 entityId；slot 不变）。
    void ConfirmSpawned(std::uint32_t spawnSlotId, std::uint64_t newEntityId);

    // "重新安家"（MoveMonsterTo 语义）：怪物被搬移时同步 slot 出生点，
    // 保证 respawn 发生在当前 home；非 slot 怪（测试临时实体）无操作。
    void RelocateSlot(std::uint64_t entityId, float x, float y);

    // 查询（测试/运维用）。
    std::uint64_t ActiveEntityOfSlot(std::uint32_t spawnSlotId) const;
    std::uint32_t SlotOfEntity(std::uint64_t entityId) const;
    bool IsPending(std::uint32_t spawnSlotId) const { return m_pending.count(spawnSlotId) != 0; }
    std::size_t PendingCount() const { return m_pending.size(); }
    std::size_t SlotCount() const { return m_slots.size(); }

    // 指令二十九：World 重启 —— 队列不持久化，全部清空（slot 配置重建）。
    void Reset();

private:
    std::vector<MonsterSpawnSlot> m_slots;
    std::unordered_map<std::uint32_t, std::uint64_t> m_activeEntityBySlot;
    std::unordered_map<std::uint64_t, std::uint32_t> m_slotByEntity;
    std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> m_pending;
};

} // namespace legend::world
