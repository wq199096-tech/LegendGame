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
        it->second.ApplySnapshot(entry.positionX, entry.positionY, entry.state,
                                 entry.targetCharacterId, batch.serverTime);
    }
    m_lastBatchSize = static_cast<std::uint32_t>(batch.monsters.size());
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
