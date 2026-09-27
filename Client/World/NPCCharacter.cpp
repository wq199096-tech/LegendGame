#include "Client/World/NPCCharacter.h"

NPCCharacter::NPCCharacter(
    legend::entity::EntityId id, std::string displayName,
    const legend::animation::CharacterDefinition& definition,
    std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> clips,
    std::shared_ptr<legend::animation::SpriteSheet> spriteSheet,
    legend::entity::Direction8 facing)
    : legend::entity::Character(id, std::move(displayName), legend::entity::ActorType::NPC,
                                definition.moveSpeed, definition.footprint,
                                legend::entity::CharacterVisual{definition.visualWidth,
                                                                definition.visualHeight,
                                                                definition.pivot},
                                std::move(clips)),
      m_definition(definition) {
    SetSpriteSheet(std::move(spriteSheet));
    SetDirection(facing); // 固定朝向：NPC 不移动，方向永不改变
    // NPC 不参与战斗（阶段5）：combatEnabled 默认 false，不可被攻击/锁定
}
