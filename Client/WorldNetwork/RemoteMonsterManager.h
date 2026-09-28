#pragma once

#include "Client/WorldNetwork/RemoteMonsterEntity.h"

#include "Shared/Monster/MonsterProtocol.h"

#include <cstdint>
#include <unordered_map>

namespace legend::client {

// 阶段13 指令五十二：RemoteMonsterManager —— 客户端远程怪物容器（主线程独占，指令六十）。
// 职责：Spawn / Despawn / ApplyBatch / UpdateInterpolation / Find / Count / Clear。
class RemoteMonsterManager {
public:
    // 不存在创建；已存在更新（指令九十八：绝不创建重复实体）。
    void HandleSpawn(const world::MonsterSpawnPayload& spawn);
    // 立即删除（指令九十九）。
    void HandleDespawn(std::uint64_t entityId);
    // 指令六十一/六十二：未知 entityId 的 snapshot 丢弃；Despawn 后旧 snapshot 因实体
    // 已删除而自然忽略。
    void HandleBatch(const world::MonsterBatchSnapshotPayload& batch);
    // 指令五十三：每帧插值（主线程）。
    void Update(float deltaTime);

    const RemoteMonsterEntity* Find(std::uint64_t entityId) const;
    std::size_t Count() const { return m_monsters.size(); }
    // 断开/重进世界时清空。
    void Clear();
    const std::unordered_map<std::uint64_t, RemoteMonsterEntity>& All() const { return m_monsters; }

    // 指令五十八：F12 Debug —— LastMonsterBatchSize。
    std::uint32_t LastBatchSize() const { return m_lastBatchSize; }

private:
    std::unordered_map<std::uint64_t, RemoteMonsterEntity> m_monsters;
    std::uint32_t m_lastBatchSize = 0;
};

} // namespace legend::client
