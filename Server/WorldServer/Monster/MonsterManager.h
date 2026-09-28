#pragma once

#include "Server/WorldServer/Monster/MonsterEntity.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace legend::world {

// 阶段13 指令十：MonsterManager —— entityId -> MonsterEntity 容器。
// io 线程与主线程（测试/Stop）并发访问——内部互斥（与 WorldManager 一致）。
class MonsterManager {
public:
    // 注册怪物（entityId 重复返回 nullptr）。
    std::shared_ptr<MonsterEntity> SpawnMonster(const std::shared_ptr<MonsterEntity>& monster);
    // 按 entityId 移除，返回被移除的怪物（不存在返回 nullptr）。
    std::shared_ptr<MonsterEntity> RemoveMonster(std::uint64_t entityId);
    std::shared_ptr<MonsterEntity> FindMonster(std::uint64_t entityId) const;
    // Stop/测试快照（拷贝 shared_ptr 列表）。
    std::vector<std::shared_ptr<MonsterEntity>> SnapshotMonsters() const;
    std::size_t Count() const;
    void Clear();

private:
    mutable std::mutex m_mutex;
    std::map<std::uint64_t, std::shared_ptr<MonsterEntity>> m_monsters;
};

} // namespace legend::world
