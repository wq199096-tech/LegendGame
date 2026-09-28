#pragma once

#include "Client/WorldNetwork/RemoteMonsterEntity.h"

#include "Shared/Combat/CombatTypes.h"
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
    // 阶段14 指令六十五：CombatEvent -> 目标怪物 HP 更新（targetType=Monster 才处理）。
    void HandleCombatEvent(std::uint64_t eventId, std::uint8_t targetType,
                           std::uint64_t targetId, std::uint32_t targetHpAfter, bool killed);
    // 阶段14 指令七十二：MonsterDeath -> alive=false（保留实体直到 Despawn）。
    void HandleDeath(std::uint64_t entityId);
    // 阶段14 指令六十八：HealthSnapshot 纠偏（已知实体直接覆盖）。
    void ApplyHealthSnapshot(std::uint64_t entityId, std::uint32_t currentHp, std::uint32_t maxHp,
                             bool alive);
    // 阶段16 指令六十三：状态事件转发（未知实体忽略）。
    void ApplyStatus(std::uint64_t entityId, const RemoteStatusEffect& effect);
    void UpdateStatus(std::uint64_t entityId, std::uint64_t instanceId, std::uint8_t stacks,
                      std::uint32_t remainingMs);
    void RemoveStatus(std::uint64_t entityId, std::uint64_t instanceId);
    void SnapshotStatus(std::uint64_t entityId, const std::vector<RemoteStatusEffect>& effects);
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
