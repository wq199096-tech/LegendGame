#include "Client/Character/PlayerCharacter.h"

PlayerCharacter::PlayerCharacter(
    legend::entity::EntityId id, const legend::animation::CharacterDefinition& definition,
    std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> clips,
    std::shared_ptr<legend::animation::SpriteSheet> spriteSheet)
    : legend::entity::Character(id, definition.name, legend::entity::ActorType::Player,
                                definition.moveSpeed, definition.footprint,
                                legend::entity::CharacterVisual{definition.visualWidth,
                                                                definition.visualHeight,
                                                                definition.pivot},
                                std::move(clips)),
      m_definition(definition) {
    SetSpriteSheet(std::move(spriteSheet));
    // 战斗组件：来自 character.json combat 块（NPC 无 combat 块则不参战）
    SetCombatEnabled(definition.hasCombat);
    if (definition.hasCombat) {
        GetCombatStats() = definition.combat;
    }
    // 阶段6：成长组件（growth 块，缺省 20/5/2）+ 空背包（20 格）
    m_progression.Initialize(definition);
}

std::vector<legend::progression::LevelUpEvent> PlayerCharacter::AddExperience(int amount) {
    return m_progression.AddExperience(GetCombatStats(), amount);
}
