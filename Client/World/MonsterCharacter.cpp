#include "Client/World/MonsterCharacter.h"

namespace legend::world {

const char* MonsterAIStateName(MonsterAIState state) {
    switch (state) {
    case MonsterAIState::Idle: return "Idle";
    case MonsterAIState::Wander: return "Wander";
    case MonsterAIState::Chase: return "Chase";
    case MonsterAIState::ReturnHome: return "ReturnHome";
    default: return "Unknown";
    }
}

MonsterCharacter::MonsterCharacter(
    legend::entity::EntityId id, const MonsterDefinition& definition,
    const legend::animation::CharacterDefinition& charDefinition,
    std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> clips,
    std::shared_ptr<legend::animation::SpriteSheet> spriteSheet,
    uint32_t spawnAreaId, const math::Vector2& spawnPosition)
    : legend::entity::Character(id, definition.name, legend::entity::ActorType::Monster,
                                charDefinition.moveSpeed, charDefinition.footprint,
                                legend::entity::CharacterVisual{charDefinition.visualWidth,
                                                                charDefinition.visualHeight,
                                                                charDefinition.pivot},
                                std::move(clips)),
      m_templateId(definition.id),
      m_displayName(definition.name),
      m_spawnPosition(spawnPosition),
      m_homePosition(spawnPosition), // 出生点即 home（Leash 中心）
      m_spawnAreaId(spawnAreaId),
      m_aggroRange(definition.ai.aggroRange),
      m_leashRange(definition.ai.leashRange),
      m_wanderRadius(definition.ai.wanderRadius),
      m_wanderIntervalMin(definition.ai.wanderIntervalMin),
      m_wanderIntervalMax(definition.ai.wanderIntervalMax),
      m_stopDistance(definition.ai.stopDistance),
      m_resumeDistance(definition.ai.resumeDistance) {
    SetSpriteSheet(std::move(spriteSheet));
    SetPosition(spawnPosition);
}

} // namespace legend::world
