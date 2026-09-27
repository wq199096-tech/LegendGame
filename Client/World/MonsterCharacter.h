#pragma once

#include <cstdint>
#include <string>

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/AnimationLoader.h"
#include "Engine/Entity/ActorType.h"
#include "Engine/Entity/Character.h"
#include "Engine/Entity/TargetHandle.h"
#include "Engine/Math/Vector2.h"
#include "Client/World/MonsterDefinition.h"

namespace legend::world {

// 怪物 AI 状态（本阶段不做 Attack/Dead/Hit）
enum class MonsterAIState : uint8_t {
    Idle = 0,
    Wander = 1,
    Chase = 2,
    ReturnHome = 3,
};

const char* MonsterAIStateName(MonsterAIState state);

// 怪物角色：模板数据 + 出生信息 + 目标句柄 + AI 参数。
// 行为逻辑不在本类（属于 MonsterAIController），只保存状态数据。
class MonsterCharacter final : public legend::entity::Character {
public:
    MonsterCharacter(legend::entity::EntityId id, const MonsterDefinition& definition,
                     const legend::animation::CharacterDefinition& charDefinition,
                     std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> clips,
                     std::shared_ptr<legend::animation::SpriteSheet> spriteSheet,
                     uint32_t spawnAreaId, const math::Vector2& spawnPosition);

    // ---- 模板与出生信息 ----
    const std::string& GetMonsterTemplateId() const { return m_templateId; }
    const std::string& GetMonsterDisplayName() const { return m_displayName; }
    const math::Vector2& GetSpawnPosition() const { return m_spawnPosition; }
    const math::Vector2& GetHomePosition() const { return m_homePosition; }
    uint32_t GetSpawnAreaId() const { return m_spawnAreaId; }

    // ---- AI 参数（来自 monster.json） ----
    float GetAggroRange() const { return m_aggroRange; }
    float GetLeashRange() const { return m_leashRange; }
    float GetWanderRadius() const { return m_wanderRadius; }
    float GetWanderIntervalMin() const { return m_wanderIntervalMin; }
    float GetWanderIntervalMax() const { return m_wanderIntervalMax; }
    float GetStopDistance() const { return m_stopDistance; }
    float GetResumeDistance() const { return m_resumeDistance; }

    // ---- AI 状态（数据由 MonsterAIController 写入） ----
    MonsterAIState GetAIState() const { return m_aiState; }
    void SetAIState(MonsterAIState state) { m_aiState = state; }

    legend::entity::TargetHandle& GetTargetHandle() { return m_currentTarget; }
    const legend::entity::TargetHandle& GetTargetHandle() const { return m_currentTarget; }

private:
    std::string m_templateId;
    std::string m_displayName;
    math::Vector2 m_spawnPosition{0.0f, 0.0f};
    math::Vector2 m_homePosition{0.0f, 0.0f};
    uint32_t m_spawnAreaId = 0;
    float m_aggroRange = 300.0f;
    float m_leashRange = 600.0f;
    float m_wanderRadius = 180.0f;
    float m_wanderIntervalMin = 2.0f;
    float m_wanderIntervalMax = 5.0f;
    float m_stopDistance = 60.0f;
    float m_resumeDistance = 80.0f;
    MonsterAIState m_aiState = MonsterAIState::Idle;
    legend::entity::TargetHandle m_currentTarget;
};

} // namespace legend::world
