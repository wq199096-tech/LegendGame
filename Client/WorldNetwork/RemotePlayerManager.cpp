#include "Client/WorldNetwork/RemotePlayerManager.h"

namespace legend::client {

void RemotePlayerManager::HandleSpawn(const world::PlayerSpawnPayload& spawn) {
    // 指令三十四：operator[] + ApplySpawn —— 已存在只更新，不创建重复实体。
    m_players[spawn.characterId].ApplySpawn(spawn);
}

void RemotePlayerManager::HandleDespawn(std::uint64_t characterId) {
    m_players.erase(characterId); // 指令三十五：立即删除
}

void RemotePlayerManager::HandleBatch(const world::RemotePlayerBatchSnapshotPayload& batch) {
    for (const auto& entry : batch.players) {
        const auto it = m_players.find(entry.characterId);
        if (it == m_players.end()) {
            continue; // 指令四十八/八十七：未知 characterId snapshot 丢弃（不创建实体）
        }
        it->second.ApplySnapshot(entry.positionX, entry.positionY, batch.serverTime);
    }
    // 指令七十二：记录最后 batch 大小（F12 Debug）。
    m_lastBatchSize = static_cast<std::uint32_t>(batch.players.size());
}

void RemotePlayerManager::Update(float deltaTime) {
    for (auto& [characterId, entity] : m_players) {
        entity.UpdateInterpolation(deltaTime);
    }
}

const RemotePlayerEntity* RemotePlayerManager::Find(std::uint64_t characterId) const {
    const auto it = m_players.find(characterId);
    return it != m_players.end() ? &it->second : nullptr;
}

void RemotePlayerManager::Clear() {
    m_players.clear();
    m_lastBatchSize = 0;
}

} // namespace legend::client
