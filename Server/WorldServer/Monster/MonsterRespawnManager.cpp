#include "Server/WorldServer/Monster/MonsterRespawnManager.h"

namespace legend::world {

void MonsterRespawnManager::InitializeFromTable(
    const MonsterSpawnPoint (&table)[kInitialMonsterCount], std::uint32_t monsterTypeId,
    std::uint16_t mapId, std::uint32_t respawnDelayMs) {
    m_slots.clear();
    m_slots.reserve(kInitialMonsterCount);
    for (std::uint32_t i = 0; i < kInitialMonsterCount; ++i) {
        MonsterSpawnSlot slot;
        slot.spawnSlotId = i + 1; // 指令二十：slotId 1~20
        slot.monsterTypeId = monsterTypeId;
        slot.mapId = mapId;
        slot.spawnX = table[i].x;
        slot.spawnY = table[i].y;
        slot.respawnDelayMs = respawnDelayMs;
        m_slots.push_back(slot);
    }
    m_activeEntityBySlot.clear();
    m_slotByEntity.clear();
    m_pending.clear();
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
