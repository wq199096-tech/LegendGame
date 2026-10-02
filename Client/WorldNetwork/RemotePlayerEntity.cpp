#include "Client/WorldNetwork/RemotePlayerEntity.h"

#include <cmath>

namespace legend::client {

namespace {
// 指令三十七：线性插值速率（1 - exp(-12*dt)）。
constexpr float kRemoteInterpolationRate = 12.0f;
// 指令四十一：服务器位置差超过该值视为移动（Walk），否则 Idle。
constexpr float kMovingEpsilonSq = 0.01f;
} // namespace

using legend::world::kRemoteTeleportDistance;

void RemotePlayerEntity::ApplySpawn(const world::PlayerSpawnPayload& spawn) {
    m_characterId = spawn.characterId;
    m_name = spawn.name;
    m_classId = spawn.classId;
    m_gender = spawn.gender;
    m_level = spawn.level;
    m_visualId = spawn.visualId;   // Stage27 指令五：真实造型（服务器权威）
    m_direction = spawn.direction; // Stage27 指令四：朝向
    m_mapId = spawn.mapId;
    // 阶段14 指令十六/六十四：Spawn 携带 HP。
    m_currentHp = spawn.currentHp;
    m_maxHp = spawn.maxHp;
    m_alive = spawn.alive;
    if (!m_active) {
        // 首次 Spawn：render 直接落在服务器位置（无历史插值状态）。
        m_serverX = spawn.positionX;
        m_serverY = spawn.positionY;
        m_renderX = spawn.positionX;
        m_renderY = spawn.positionY;
        m_moving = false;
    } else {
        // 指令三十四：已存在 -> 只更新元数据与服务器目标位置（render 继续插值）。
        m_serverX = spawn.positionX;
        m_serverY = spawn.positionY;
    }
    m_lastSnapshotServerTime = spawn.serverTime;
    m_active = true;
}

void RemotePlayerEntity::ApplySnapshot(float serverX, float serverY, std::uint64_t serverTime,
                                       std::uint8_t direction) {
    if (!m_active) {
        return; // 指令四十八：Spawn 前的 snapshot 由 Manager 丢弃，防御性兜底
    }
    const float deltaX = serverX - m_serverX;
    const float deltaY = serverY - m_serverY;
    // 指令四十一：相邻两次服务器位置差估算移动方向（Walk/Idle）。
    m_moving = (deltaX * deltaX + deltaY * deltaY) > kMovingEpsilonSq;
    // Stage27 指令四：服务器权威朝向（移动中由客户端位置差分驱动表现；
    // 静止时以服务器朝向为准，保证转头可见）。
    m_direction = direction;
    m_serverX = serverX;
    m_serverY = serverY;
    m_lastSnapshotServerTime = serverTime;
}

void RemotePlayerEntity::UpdateInterpolation(float deltaTime) {
    if (!m_active) {
        return;
    }
    const float deltaX = m_serverX - m_renderX;
    const float deltaY = m_serverY - m_renderY;
    const float distanceSq = deltaX * deltaX + deltaY * deltaY;
    if (distanceSq > kRemoteTeleportDistance * kRemoteTeleportDistance) {
        // 指令三十八：>300 视为 teleport/correction，直接 snap。
        m_renderX = m_serverX;
        m_renderY = m_serverY;
        return;
    }
    const float t = 1.0f - std::exp(-kRemoteInterpolationRate * deltaTime);
    m_renderX += deltaX * t;
    m_renderY += deltaY * t;
}

} // namespace legend::client
