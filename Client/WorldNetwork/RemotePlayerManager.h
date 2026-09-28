#pragma once

#include "Client/WorldNetwork/RemotePlayerEntity.h"

#include "Shared/Combat/CombatTypes.h"
#include "Shared/World/WorldProtocol.h"

#include <cstdint>
#include <unordered_map>

namespace legend::client {

// 阶段12 指令三十三：RemotePlayerManager —— 客户端远程玩家容器（主线程独占，指令四十七）。
// 职责：Spawn / Despawn / ApplySnapshot / UpdateInterpolation / Find / Count。
class RemotePlayerManager {
public:
    // 指令三十四：不存在创建；已存在更新（绝不创建重复实体）。
    void HandleSpawn(const world::PlayerSpawnPayload& spawn);
    // 指令三十五：立即删除（不做淡出）。
    void HandleDespawn(std::uint64_t characterId);
    // 指令三十（接收侧）/四十八/四十九：未知 characterId 的 snapshot 丢弃；
    // Despawn 后到达的旧 snapshot 因实体已删除而自然忽略。
    void HandleBatch(const world::RemotePlayerBatchSnapshotPayload& batch);
    // 阶段14 指令六十五：targetType=Player 的 CombatEvent -> 远程玩家 HP 更新。
    void HandleCombatEvent(std::uint8_t targetType, std::uint64_t targetId,
                           std::uint32_t targetHpAfter, std::uint32_t targetMaxHp, bool killed);
    // 阶段14 指令七十三：PlayerDeath -> 对应远程玩家 alive=false。
    void HandleDeath(std::uint64_t characterId);
    // 阶段14 指令六十八：HealthSnapshot 纠偏（已知实体直接覆盖）。
    void ApplyHealthSnapshot(std::uint64_t characterId, std::uint32_t currentHp,
                             std::uint32_t maxHp, bool alive);
    // 阶段15 指令五十七：远程玩家技能表现状态（未知 characterId 忽略）。
    void ApplyCastState(std::uint64_t characterId, bool casting, std::uint32_t skillId,
                        std::uint64_t startServerTime, std::uint32_t durationMs);
    // 阶段16 指令六十二：状态事件转发（未知 characterId 忽略）。
    void ApplyStatus(std::uint64_t characterId, const RemoteStatusEffect& effect);
    void UpdateStatus(std::uint64_t characterId, std::uint64_t instanceId, std::uint8_t stacks,
                      std::uint32_t remainingMs);
    void RemoveStatus(std::uint64_t characterId, std::uint64_t instanceId);
    void SnapshotStatus(std::uint64_t characterId, const std::vector<RemoteStatusEffect>& effects);
    // 指令三十七：每帧插值（主线程）。
    void Update(float deltaTime);

    const RemotePlayerEntity* Find(std::uint64_t characterId) const;
    std::size_t Count() const { return m_players.size(); }
    // 指令五十九（客户端侧）：连接断开 -> 清空全部远程实体。
    void Clear();
    const std::unordered_map<std::uint64_t, RemotePlayerEntity>& All() const { return m_players; }

    // 指令七十二：F12 Debug —— LastRemoteBatchSize。
    std::uint32_t LastBatchSize() const { return m_lastBatchSize; }

private:
    std::unordered_map<std::uint64_t, RemotePlayerEntity> m_players;
    std::uint32_t m_lastBatchSize = 0;
};

} // namespace legend::client
