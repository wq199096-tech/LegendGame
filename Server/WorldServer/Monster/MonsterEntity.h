#pragma once

#include "Shared/Monster/MonsterTypes.h"

#include <chrono>
#include <cstdint>

namespace legend::world {

// 阶段13 指令六/七/八/九：MonsterEntity —— 服务器权威怪物运行时实体。
// runtime only（指令六十六：不进数据库）；entityId 由 WorldServer 单调计数器生成。
// 禁止 Attack/Cast/Hit/Dead 状态（指令九）；移动/状态全部服务器权威（指令一）。
class MonsterEntity {
public:
    MonsterEntity() = default;
    MonsterEntity(std::uint64_t entityId, std::uint32_t monsterTypeId, std::uint16_t mapId,
                  float spawnX, float spawnY, float moveSpeed);

    std::uint64_t EntityId() const { return m_entityId; }
    std::uint32_t MonsterTypeId() const { return m_monsterTypeId; }
    std::uint16_t MapId() const { return m_mapId; }

    float PositionX() const { return m_positionX; }
    float PositionY() const { return m_positionY; }
    void SetPosition(float x, float y) {
        m_positionX = x;
        m_positionY = y;
    }

    float SpawnX() const { return m_spawnX; }
    float SpawnY() const { return m_spawnY; }
    float MoveSpeed() const { return m_moveSpeed; }

    MonsterState State() const { return m_state; }
    void SetState(MonsterState state) { m_state = state; }

    std::uint64_t TargetCharacterId() const { return m_targetCharacterId; }
    void SetTargetCharacterId(std::uint64_t targetCharacterId) {
        m_targetCharacterId = targetCharacterId;
    }

    // 进入当前状态的时刻（Idle 2s 计时用，指令三十二）。
    std::chrono::steady_clock::time_point StateEnterTime() const { return m_stateEnterTime; }
    void TouchStateEnterTime() { m_stateEnterTime = std::chrono::steady_clock::now(); }

    // Patrol 目标点（指令三十三/三十四）。
    float PatrolTargetX() const { return m_patrolTargetX; }
    float PatrolTargetY() const { return m_patrolTargetY; }
    void SetPatrolTarget(float x, float y) {
        m_patrolTargetX = x;
        m_patrolTargetY = y;
    }
    // 指令三十四：确定性 patrol 序号（entityId + patrolSequence 决定角度）。
    std::uint32_t PatrolSequence() const { return m_patrolSequence; }
    void AdvancePatrolSequence() { ++m_patrolSequence; }

    bool IsActive() const { return m_active; }
    void SetActive(bool active) { m_active = active; }

    std::chrono::steady_clock::time_point LastAiTime() const { return m_lastAiTime; }
    void TouchAiTime() { m_lastAiTime = std::chrono::steady_clock::now(); }
    std::chrono::steady_clock::time_point LastSnapshotTime() const { return m_lastSnapshotTime; }
    void TouchSnapshotTime() { m_lastSnapshotTime = std::chrono::steady_clock::now(); }

private:
    std::uint64_t m_entityId = 0;
    std::uint32_t m_monsterTypeId = 0;
    std::uint16_t m_mapId = 1;
    float m_positionX = 0.0f;
    float m_positionY = 0.0f;
    float m_spawnX = 0.0f;
    float m_spawnY = 0.0f;
    float m_moveSpeed = 80.0f;
    MonsterState m_state = MonsterState::Idle;
    std::uint64_t m_targetCharacterId = 0;
    float m_patrolTargetX = 0.0f;
    float m_patrolTargetY = 0.0f;
    std::uint32_t m_patrolSequence = 0;
    std::chrono::steady_clock::time_point m_stateEnterTime{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point m_lastAiTime{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point m_lastSnapshotTime{std::chrono::steady_clock::now()};
    bool m_active = true;
};

} // namespace legend::world
