#include "Client/Character/PlayerCharacter.h"

PlayerCharacter::PlayerCharacter(
    legend::entity::EntityId id, const legend::animation::CharacterDefinition& definition,
    std::shared_ptr<const std::unordered_map<std::string, legend::animation::AnimationClip>> clips,
    std::shared_ptr<legend::animation::SpriteSheet> spriteSheet)
    : legend::entity::Character(id, definition.name, definition.moveSpeed, definition.footprint,
                                legend::entity::CharacterVisual{definition.visualWidth,
                                                                definition.visualHeight,
                                                                definition.pivot},
                                std::move(clips)),
      m_definition(definition) {
    SetSpriteSheet(std::move(spriteSheet));
}
