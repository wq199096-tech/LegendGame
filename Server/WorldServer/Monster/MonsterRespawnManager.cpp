#include "Server/WorldServer/Monster/MonsterRespawnManager.h"

namespace legend::world {

void MonsterRespawnManager::InitializeFromTable(
    const MonsterSpawnPoint (&table)[kInitialMonsterCount], std::uint32_t monsterTypeId,
    std::uint16_t mapId, std::uint32_t respawnDelayMs) {
    Reset();
    const std::vector<MonsterSpawnPoint> points(table, table + kInitialMonsterCount);
    InitializeFromPoints(points, monsterTypeId, mapId, respawnDelayMs, 1);
}

// 阶段21 指令十一：按地图追加布点（slotId 从 firstSlotId 起单调；多地图共用容器；
// 调用方先 Reset() 再逐图追加——World 重启时全量重建，语义不变）。
void MonsterRespawnManager::InitializeFromPoints(const std::vector<MonsterSpawnPoint>& points,
                                                 std::uint32_t monsterTypeId,
                                                 std::uint16_t mapId,
                                                 std::uint32_t respawnDelayMs,
                                                 std::uint32_t firstSlotId) {
    m_slots.reserve(m_slots.size() + points.size());
    std::uint32_t slotId = firstSlotId;
    for (const auto& point : points) {
        MonsterSpawnSlot slot;
        slot.spawnSlotId = slotId++;
        slot.monsterTypeId = monsterTypeId;
        slot.mapId = mapId;
        slot.spawnX = point.x;
        slot.spawnY = point.y;
        slot.respawnDelayMs = respawnDelayMs;
        m_slots.push_back(slot);
    }
}

const MonsterSpawnSlot* MonsterRespawnManager::FindSlot(std::uint32_t spawnSlotId) const {
    for (const auto& slot : m_slots) {
        if (slot.spawnSlotId == spawnSlotId) {
            return &slot;
        }
    }
    return nullptr;
}

void MonsterRespawnManager::BindEntity(std::uint32_t spawnSlotId, std::uint64_t entityId) {
    m_activeEntityBySlot[spawnSlotId] = entityId;
    m_slotByEntity[entityId] = spawnSlotId;
}

void MonsterRespawnManager::NotifyEntityRemoved(std::uint64_t entityId,
                                                std::chrono::steady_clock::time_point deathTime) {
    const auto slotIt = m_slotByEntity.find(entityId);
    if (slotIt == m_slotByEntity.end()) {
        return; // 非 slot 怪（测试临时移除）：不重生
    }
    const std::uint32_t slotId = slotIt->second;
    m_slotByEntity.erase(slotIt);
    m_activeEntityBySlot.erase(slotId);
    const MonsterSpawnSlot* slot = FindSlot(slotId);
    if (!slot) {
        return;
    }
    // 指令二十五：只有该 slot 当前没有 active entity 且未在队列才入队（防重复复活）。
    if (m_activeEntityBySlot.count(slotId) != 0 || m_pending.count(slotId) != 0) {
        return;
    }
    m_pending[slotId] = deathTime + std::chrono::milliseconds(slot->respawnDelayMs);
}

std::vector<MonsterRespawnManager::RespawnDue> MonsterRespawnManager::Poll(
    std::chrono::steady_clock::time_point now) {
    std::vector<RespawnDue> due;
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (now < it->second) {
            ++it;
            continue;
        }
        // 指令二十五：slot 仍被占用（异常场景）-> 放弃本次重生，防重复复活。
        if (m_activeEntityBySlot.count(it->first) != 0) {
            it = m_pending.erase(it);
            continue;
        }
        const MonsterSpawnSlot* slot = FindSlot(it->first);
        if (!slot) {
            it = m_pending.erase(it);
            continue;
        }
        due.push_back({it->first, *slot});
        it = m_pending.erase(it);
    }
    return due;
}

void MonsterRespawnManager::ConfirmSpawned(std::uint32_t spawnSlotId, std::uint64_t newEntityId) {
    BindEntity(spawnSlotId, newEntityId);
}

void MonsterRespawnManager::RelocateSlot(std::uint64_t entityId, float x, float y) {
    const auto slotIt = m_slotByEntity.find(entityId);
    if (slotIt == m_slotByEntity.end()) {
        return; // 非 slot 怪：无出生点可搬
    }
    for (auto& slot : m_slots) {
        if (slot.spawnSlotId == slotIt->second) {
            slot.spawnX = x;
            slot.spawnY = y;
            return;
        }
    }
}

std::uint64_t MonsterRespawnManager::ActiveEntityOfSlot(std::uint32_t spawnSlotId) const {
    const auto it = m_activeEntityBySlot.find(spawnSlotId);
    return it != m_activeEntityBySlot.end() ? it->second : 0;
}

std::uint32_t MonsterRespawnManager::SlotOfEntity(std::uint64_t entityId) const {
    const auto it = m_slotByEntity.find(entityId);
    return it != m_slotByEntity.end() ? it->second : 0;
}

void MonsterRespawnManager::Reset() {
    m_activeEntityBySlot.clear();
    m_slotByEntity.clear();
    m_pending.clear();
}

} // namespace legend::world
