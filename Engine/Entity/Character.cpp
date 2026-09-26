#include "Engine/Entity/Character.h"

namespace legend::entity {

Character::Character(EntityId id, std::string name, float moveSpeed,
                     const CharacterFootprint& footprint, const CharacterVisual& visual,
                     std::shared_ptr<const std::unordered_map<std::string, animation::AnimationClip>> clips)
    : Entity(id, std::move(name)),
      m_moveSpeed(moveSpeed),
      m_footprint(footprint),
      m_visual(visual) {
    m_animationPlayer.SetClips(std::move(clips));
}

} // namespace legend::entity
