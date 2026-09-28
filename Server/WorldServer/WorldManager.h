#pragma once

#include "Server/WorldServer/PlayerSession.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace legend::world {

// 阶段11 指令二十八/二十九/一百零七：WorldManager。
// 维护 characterId -> PlayerSession 与 connectionId -> PlayerSession。
// 同一 characterId 不允许两个在线（重复 EnterWorld 拒绝）。
// io 线程与主线程（Stop flush）并发访问——内部互斥。
class WorldManager {
public:
    // 尝试注册在线玩家；characterId 已在线返回 nullptr（CharacterAlreadyOnline）。
    std::shared_ptr<PlayerSession> TryAddPlayer(const std::shared_ptr<PlayerSession>& player);
    // 按 connectionId 移除，返回被移除的玩家（不存在返回 nullptr）。
    std::shared_ptr<PlayerSession> RemoveByConnection(std::uint64_t connectionId);
    std::shared_ptr<PlayerSession> FindByConnection(std::uint64_t connectionId) const;
    std::shared_ptr<PlayerSession> FindByCharacter(std::uint64_t characterId) const;
    std::size_t PlayerCount() const;

    // 阶段11 指令一百一十二：Stop 时快照全部玩家（Flush Save 用）。
    std::vector<std::shared_ptr<PlayerSession>> SnapshotPlayers() const;

private:
    mutable std::mutex m_mutex;
    std::map<std::uint64_t, std::shared_ptr<PlayerSession>> m_byCharacter;
    std::map<std::uint64_t, std::shared_ptr<PlayerSession>> m_byConnection;
};

} // namespace legend::world
