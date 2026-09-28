#include "Server/WorldServer/Monster/MonsterManager.h"

namespace legend::world {

std::shared_ptr<MonsterEntity> MonsterManager::SpawnMonster(
    const std::shared_ptr<MonsterEntity>& monster) {
    if (!monster) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_monsters.find(monster->EntityId());
    if (it != m_monsters.end()) {
        return nullptr; // entityId 必须唯一（指令七）
    }
    m_monsters[monster->EntityId()] = monster;
    return monster;
}

std::shared_ptr<MonsterEntity> MonsterManager::RemoveMonster(std::uint64_t entityId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_monsters.find(entityId);
    if (it == m_monsters.end()) {
        return nullptr;
    }
    auto monster = it->second;
    m_monsters.erase(it);
    return monster;
}

std::shared_ptr<MonsterEntity> MonsterManager::FindMonster(std::uint64_t entityId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_monsters.find(entityId);
    return it != m_monsters.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<MonsterEntity>> MonsterManager::SnapshotMonsters() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::shared_ptr<MonsterEntity>> monsters;
    monsters.reserve(m_monsters.size());
    for (auto& [id, monster] : m_monsters) {
        monsters.push_back(monster);
    }
    return monsters;
}

std::size_t MonsterManager::Count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_monsters.size();
}

void MonsterManager::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_monsters.clear();
}

} // namespace legend::world
