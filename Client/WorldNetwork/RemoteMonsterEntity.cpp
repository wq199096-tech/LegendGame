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

void RemoteMonsterEntity::ApplySnapshot(float serverX, float serverY, std::uint8_t state,
                                        std::uint64_t targetCharacterId,
                                        std::uint64_t serverTime) {
    if (!m_active) {
        return; // 指令六十一：Spawn 前的 snapshot 由 Manager 丢弃，防御性兜底
    }
    (void)serverTime;
    m_state = state;                        // 指令五十五：状态随服务器权威快照
    m_targetCharacterId = targetCharacterId;
    m_serverX = serverX;
    m_serverY = serverY;
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
