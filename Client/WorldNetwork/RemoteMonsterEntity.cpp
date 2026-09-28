#include "Client/WorldNetwork/RemoteMonsterEntity.h"

#include "Shared/World/WorldTypes.h"

#include <cmath>

namespace legend::client {

namespace {
// 指令五十三：与 RemotePlayer 相同的插值速率。
constexpr float kMonsterInterpolationRate = 12.0f;
} // namespace

using legend::world::kRemoteTeleportDistance;

void RemoteMonsterEntity::ApplySpawn(const world::MonsterSpawnPayload& spawn) {
    m_entityId = spawn.entityId;
    m_monsterTypeId = spawn.monsterTypeId;
    m_name = spawn.name;
    m_level = spawn.level;
    m_mapId = spawn.mapId;
    m_state = spawn.state;
    // 阶段14 指令十五/六十三：Spawn 携带 HP。
    m_currentHp = spawn.currentHp;
    m_maxHp = spawn.maxHp;
    m_alive = spawn.alive;
    if (!m_active) {
        // 首次 Spawn：render 直接落在服务器位置（无历史插值状态）。
        m_serverX = spawn.positionX;
        m_serverY = spawn.positionY;
        m_renderX = spawn.positionX;
        m_renderY = spawn.positionY;
    } else {
        m_serverX = spawn.positionX;
        m_serverY = spawn.positionY;
    }
    m_active = true;
}

void RemoteMonsterEntity::ApplySnapshot(const world::MonsterSnapshotEntry& entry,
                                        std::uint64_t serverTime) {
    if (!m_active) {
        return; // 指令六十一：Spawn 前的 snapshot 由 Manager 丢弃，防御性兜底
    }
    (void)serverTime;
    m_state = entry.state;                  // 指令五十五：状态随服务器权威快照
    m_targetCharacterId = entry.targetCharacterId;
    // 阶段14 指令九十一：快照携带 HP（Dead 怪 alive=false 继续同步）。
    m_currentHp = entry.currentHp;
    m_maxHp = entry.maxHp;
    m_alive = entry.alive;
    m_serverX = entry.positionX;
    m_serverY = entry.positionY;
}

void RemoteMonsterEntity::ApplyCombatEvent(std::uint64_t eventId, std::uint32_t hpAfter,
                                           bool killed) {
    if (!m_active) {
        return;
    }
    // 阶段14 指令六十七：eventId <= lastCombatEventId 的旧包忽略（乱序保护）。
    if (eventId <= m_lastCombatEventId) {
        return;
    }
    m_lastCombatEventId = eventId;
    m_currentHp = hpAfter;
    if (killed) {
        m_alive = false; // 指令七十二：死亡后保留实体直到 MonsterDespawn
    }
}

void RemoteMonsterEntity::UpdateInterpolation(float deltaTime) {
    if (!m_active) {
        return;
    }
    const float deltaX = m_serverX - m_renderX;
    const float deltaY = m_serverY - m_renderY;
    const float distanceSq = deltaX * deltaX + deltaY * deltaY;
    if (distanceSq > kRemoteTeleportDistance * kRemoteTeleportDistance) {
        // 指令五十四：>300 直接 snap。
        m_renderX = m_serverX;
        m_renderY = m_serverY;
        return;
    }
    const float t = 1.0f - std::exp(-kMonsterInterpolationRate * deltaTime);
    m_renderX += deltaX * t;
    m_renderY += deltaY * t;
}

} // namespace legend::client
