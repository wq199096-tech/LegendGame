#include "Client/WorldNetwork/RemoteMonsterManager.h"

namespace legend::client {

void RemoteMonsterManager::HandleSpawn(const world::MonsterSpawnPayload& spawn) {
    m_monsters[spawn.entityId].ApplySpawn(spawn);
}

void RemoteMonsterManager::HandleDespawn(std::uint64_t entityId) {
    m_monsters.erase(entityId);
}

void RemoteMonsterManager::HandleBatch(const world::MonsterBatchSnapshotPayload& batch) {
    for (const auto& entry : batch.monsters) {
        const auto it = m_monsters.find(entry.entityId);
        if (it == m_monsters.end()) {
            continue; // 指令六十一/九十七：未知 entityId snapshot 丢弃（不创建实体）
        }
        it->second.ApplySnapshot(entry, batch.serverTime);
    }
    m_lastBatchSize = static_cast<std::uint32_t>(batch.monsters.size());
}

void RemoteMonsterManager::HandleCombatEvent(std::uint64_t eventId,
                                             std::uint8_t targetType,
                                             std::uint64_t targetId,
                                             std::uint32_t targetHpAfter, bool killed) {
    // 阶段14 指令六十五：只处理 targetType=Monster 的 CombatEvent（Player 目标走
    // RemotePlayerManager / 本地 HP）。
    if (targetType != static_cast<std::uint8_t>(legend::world::CombatEntityType::Monster)) {
        return;
    }
    const auto it = m_monsters.find(targetId);
    if (it == m_monsters.end()) {
        return; // 未知目标丢弃
    }
    it->second.ApplyCombatEvent(eventId, targetHpAfter, killed);
}

void RemoteMonsterManager::HandleDeath(std::uint64_t entityId) {
    // 阶段14 指令七十二：MonsterDeath -> alive=false（保留实体直到 MonsterDespawn）。
    const auto it = m_monsters.find(entityId);
    if (it != m_monsters.end()) {
        it->second.SetAliveLocal(false);
    }
}

void RemoteMonsterManager::ApplyHealthSnapshot(std::uint64_t entityId, std::uint32_t currentHp,
                                               std::uint32_t maxHp, bool alive) {
    // 阶段14 指令六十八：1s 纠偏（权威覆盖，无 eventId）。
    const auto it = m_monsters.find(entityId);
    if (it == m_monsters.end()) {
        return;
    }
    auto& entity = it->second;
    entity.SetAliveLocal(alive);
    // HP 覆盖：借助 ApplyCombatEvent 之外的直接通道（单调 eventId 不适用于快照）。
    entity.ApplySnapshotHealth(currentHp, maxHp);
}

// ---------------------------------------------------------------------------
// 阶段16 指令六十三：状态事件转发（未知实体忽略）
// ---------------------------------------------------------------------------

void RemoteMonsterManager::ApplyStatus(std::uint64_t entityId, const RemoteStatusEffect& effect) {
    const auto it = m_monsters.find(entityId);
    if (it != m_monsters.end()) {
        it->second.StatusEffects().Apply(effect);
    }
}

void RemoteMonsterManager::UpdateStatus(std::uint64_t entityId, std::uint64_t instanceId,
                                        std::uint8_t stacks, std::uint32_t remainingMs) {
    const auto it = m_monsters.find(entityId);
    if (it != m_monsters.end()) {
        it->second.StatusEffects().Update(instanceId, stacks, remainingMs);
    }
}

void RemoteMonsterManager::RemoveStatus(std::uint64_t entityId, std::uint64_t instanceId) {
    const auto it = m_monsters.find(entityId);
    if (it != m_monsters.end()) {
        it->second.StatusEffects().Remove(instanceId);
    }
}

void RemoteMonsterManager::SnapshotStatus(std::uint64_t entityId,
                                          const std::vector<RemoteStatusEffect>& effects) {
    const auto it = m_monsters.find(entityId);
    if (it != m_monsters.end()) {
        it->second.StatusEffects().SnapshotReplace(effects);
    }
}

void RemoteMonsterManager::Update(float deltaTime) {
    for (auto& [entityId, entity] : m_monsters) {
        entity.UpdateInterpolation(deltaTime);
    }
}

const RemoteMonsterEntity* RemoteMonsterManager::Find(std::uint64_t entityId) const {
    const auto it = m_monsters.find(entityId);
    return it != m_monsters.end() ? &it->second : nullptr;
}

void RemoteMonsterManager::Clear() {
    m_monsters.clear();
    m_lastBatchSize = 0;
}

} // namespace legend::client
