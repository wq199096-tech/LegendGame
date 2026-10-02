#pragma once

// ---------------------------------------------------------------------------
// Stage27 指令二十九：OnlinePlayerDirectory —— WorldServer 在线玩家目录。
// characterId -> session 与 characterName -> session 双索引（O(1) 查找），
// 用于私聊目标查找与玩家查找（指令十一/四十二：私聊直接查目录，不访问 SQLite）。
// 同名唯一性由 Stage26 角色创建规则保证；同角色重复上线由 WorldManager 拒绝。
// 线程安全：内部互斥（主要访问在 World io 线程；GUI CollectStats/Stop 允许跨线程读）。
// ---------------------------------------------------------------------------

#include "Server/WorldServer/PlayerSession.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace legend::world {

class OnlinePlayerDirectory {
public:
    void Add(const std::shared_ptr<PlayerSession>& player) {
        if (!player) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_byId[player->CharacterId()] = player;
        m_nameToId[player->CharacterName()] = player->CharacterId();
    }

    // 返回被移除的会话（不存在返回 nullptr）。
    std::shared_ptr<PlayerSession> RemoveByCharacterId(std::uint64_t characterId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_byId.find(characterId);
        if (it == m_byId.end()) {
            return nullptr;
        }
        auto removed = it->second;
        m_byId.erase(it);
        const auto nameIt = m_nameToId.find(removed->CharacterName());
        if (nameIt != m_nameToId.end() && nameIt->second == characterId) {
            m_nameToId.erase(nameIt);
        }
        return removed;
    }

    std::shared_ptr<PlayerSession> FindByCharacterId(std::uint64_t characterId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_byId.find(characterId);
        return it != m_byId.end() ? it->second : nullptr;
    }

    std::shared_ptr<PlayerSession> FindByCharacterName(const std::string& name) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto nameIt = m_nameToId.find(name);
        if (nameIt == m_nameToId.end()) {
            return nullptr;
        }
        const auto it = m_byId.find(nameIt->second);
        return it != m_byId.end() ? it->second : nullptr;
    }

    std::size_t Size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_byId.size();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_byId.clear();
        m_nameToId.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::uint64_t, std::shared_ptr<PlayerSession>> m_byId;
    std::unordered_map<std::string, std::uint64_t> m_nameToId;
};

} // namespace legend::world
