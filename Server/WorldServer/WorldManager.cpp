#include "Server/WorldServer/WorldManager.h"

namespace legend::world {

std::shared_ptr<PlayerSession> WorldManager::TryAddPlayer(
    const std::shared_ptr<PlayerSession>& player) {
    if (!player) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    // 指令二十九/一百零七：同 characterId 只能一个在线。
    if (m_byCharacter.find(player->CharacterId()) != m_byCharacter.end()) {
        return nullptr;
    }
    m_byCharacter[player->CharacterId()] = player;
    m_byConnection[player->ConnectionId()] = player;
    return player;
}

std::shared_ptr<PlayerSession> WorldManager::RemoveByConnection(std::uint64_t connectionId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_byConnection.find(connectionId);
    if (it == m_byConnection.end()) {
        return nullptr;
    }
    auto player = it->second;
    m_byConnection.erase(it);
    m_byCharacter.erase(player->CharacterId());
    return player;
}

std::shared_ptr<PlayerSession> WorldManager::FindByConnection(std::uint64_t connectionId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_byConnection.find(connectionId);
    return it != m_byConnection.end() ? it->second : nullptr;
}

std::shared_ptr<PlayerSession> WorldManager::FindByCharacter(std::uint64_t characterId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_byCharacter.find(characterId);
    return it != m_byCharacter.end() ? it->second : nullptr;
}

std::size_t WorldManager::PlayerCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_byCharacter.size();
}

std::vector<std::shared_ptr<PlayerSession>> WorldManager::SnapshotPlayers() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::shared_ptr<PlayerSession>> players;
    players.reserve(m_byCharacter.size());
    for (auto& [id, player] : m_byCharacter) {
        players.push_back(player);
    }
    return players;
}

} // namespace legend::world
